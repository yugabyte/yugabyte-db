/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/create_indexes.c
 *
 * Implementation of the create index / reindex operation.
 *
 * XXX: We also need to specially take care of the indexes on "_id"
 *      field. For example, while we should simply skip creating the
 *      index if its key is exactly equal to {"_id": 1}, we
 *      should throw an error if the spec indicates indexing "_id"
 *      field in reverse order (i.e.: {"_id": -1}).
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <funcapi.h>
#include <math.h>
#include <miscadmin.h>
#include <access/xact.h>
#include <catalog/namespace.h>
#include <executor/executor.h>
#include <executor/spi.h>
#include <lib/stringinfo.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <optimizer/optimizer.h>
#include <storage/lmgr.h>
#include <storage/lockdefs.h>
#include <storage/proc.h>
#include <tcop/pquery.h>
#include <tcop/utility.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>
#include <utils/ruleutils.h>
#include <utils/snapmgr.h>
#include <utils/syscache.h>

#include "api_hooks.h"
#include "io/helio_bson_core.h"
#include "aggregation/bson_projection_tree.h"
#include "commands/commands_common.h"
#include "commands/create_indexes.h"
#include "commands/diagnostic_commands_common.h"
#include "commands/drop_indexes.h"
#include "commands/lock_tags.h"
#include "utils/mongo_errors.h"
#include "commands/parse_error.h"
#include "geospatial/bson_geospatial_common.h"
#include "geospatial/bson_geospatial_geonear.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "planner/mongo_query_operator.h"
#include "query/query_operator.h"
#include "utils/guc_utils.h"
#include "utils/list_utils.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "utils/index_utils.h"
#include "utils/version_utils.h"
#include "vector/vector_common.h"


#define MAX_INDEX_OPTIONS_LENGTH 1500


/*
 * Represents the type of index for a given path.
 * Treat this as a flags so that we can check for
 * plugins.
 */
typedef enum MongoIndexKind
{
	/* Unknow / invalid index plugin */
	MongoIndexKind_Unknown = 0x0,

	/* Regular asc/desc index */
	MongoIndexKind_Regular = 0x1,

	/* Hashed index */
	MongoIndexKind_Hashed = 0x2,

	/* Geospatial 2D index */
	MongoIndexKind_2d = 0x4,

	/* Text search index */
	MongoIndexKind_Text = 0x8,

	/* Geospatial 2D index */
	MongoIndexKind_2dsphere = 0x10,

	/* A CosmosDB Indexing kind */
	MongoIndexingKind_CosmosSearch = 0x20,
} MongoIndexKind;


typedef struct IndexDefKeyPath
{
	/* The path constructed for the index (See IndexDefKey) */
	const char *path;

	/* The index kind for this path */
	MongoIndexKind indexKind;

	/* Whether or not this specific key is a wildcard index */
	bool isWildcard;
} IndexDefKeyPath;


/* Return value of TryCreateCollectionIndexes */
typedef struct
{
	bool ok;

	/* error reporting; valid only when "ok" is false */
	char *errmsg;
	int errcode;
} TryCreateIndexesResult;


/* Return value of TryReIndexCollectionIndexesConcurrently */
typedef struct
{
	bool ok;

	/* error reporting; valid only when "ok" is false */
	char *errmsg;
	int errcode;

	/* Used to track transient invalid indexes in case of failure, if null then no index is building*/
	IndexDetails *indexCurrentlyBuilding;
} TryReIndexesResult;

/*
 * Contains the data used when building the bson object that needs to be
 * sent to the client after a reindex() command.
 */
typedef struct
{
	bool ok;
	int nIndexesWas;
	int nIndexes;
	List *indexesDetails;

	/* error reporting; valid only when "ok" is false */
	char *errmsg;
	int errcode;
} ReIndexResult;


/* Context passed as an argument to GetPartFilterExprNodeReprWalker */
typedef struct
{
	/*
	 * Should be set to 0 for the top-level call made to
	 * GetPartFilterExprNodeReprWalker.
	 */
	int indentationLevel;

	/*
	 * Output of GetPartFilterExprNodeReprWalker.
	 *
	 * Should be initialized by top-level caller.
	 */
	StringInfo reprStr;
} PartFilterExprNodeReprWalkerContext;


typedef enum
{
	WP_IM_INVALID = 0,
	WP_IM_INCLUDE,
	WP_IM_EXCLUDE
} WildcardProjFieldInclusionMode;


/*
 * A LeafNode that is used for CreateIndex
 * WildCard projections. The leaf is the basic
 * leaf path, but also tracks the relativePath
 * to the node.
 */
typedef struct
{
	BsonLeafPathNode leafPathNode;

	const char *relativePath;
} CreateIndexesLeafPathNodeData;


/* Return value of ResolveWPPathOpsFromTree */
typedef struct
{
	WildcardProjFieldInclusionMode idFieldInclusion;
	WildcardProjFieldInclusionMode nonIdFieldInclusion;
	List *nonIdFieldPathList;
} WildcardProjectionPathOps;

typedef struct
{
	const char *mongoIndexName;
	bool isSupported;
	MongoIndexKind indexKind;
} MongoIndexSupport;


/* Different types of indexes supported by Mongo
 */
static const MongoIndexSupport MongoIndexSupportedList[] =
{
	{ "2d", true, MongoIndexKind_2d },
	{ "hashed", true, MongoIndexKind_Hashed },
	{ "text", true, MongoIndexKind_Text },
	{ "2dsphere", true, MongoIndexKind_2dsphere },
	{ "cosmosSearch", true, MongoIndexingKind_CosmosSearch },
};

static const int NumberOfMongoIndexTypes = sizeof(MongoIndexSupportedList) /
										   sizeof(MongoIndexSupport);

extern bool EnableExtendedIndexFilters;
extern bool ForceIndexTermTruncation;
extern int IndexTruncationLimitOverride;
extern bool DefaultEnableLargeIndexKeys;

#define WILDCARD_INDEX_SUFFIX "$**"
#define DOT_WILDCARD_INDEX_SUFFIX "." WILDCARD_INDEX_SUFFIX
#define DOUBLE_DOT_IN_INDEX_PATH ".."

#define REINDEX_SUCCESSFUL_DEBUGMSG \
	"reindexed all collection indexes"

/* We hardcode the rum index size limit at the point in time we defined the limit to not break backward compat if RUM suddenly changes the limit to allow larger index terms. */
#define _RUM_TERM_SIZE_LIMIT 2712

/* Simple index terms have an overhead of 8 bytes so we need to substract that from the actual limit */
#define SINGLE_PATH_INDEX_TERM_SIZE_LIMIT (uint32_t) (_RUM_TERM_SIZE_LIMIT - \
													  (sizeof(uint8_t) + VARHDRSZ + 8))

/* Compound index terms have an overhead of 16 bytes so we need to substract that from the actual limit */
#define COMPOUND_INDEX_TERM_SIZE_LIMIT (uint32_t) (_RUM_TERM_SIZE_LIMIT - \
												   (sizeof(uint8_t) + VARHDRSZ + 16))

/* exported only for testing purposes */
PG_FUNCTION_INFO_V1(generate_create_index_arg);

PG_FUNCTION_INFO_V1(command_create_indexes_non_concurrently);
PG_FUNCTION_INFO_V1(command_create_temp_indexes_non_concurrently);
PG_FUNCTION_INFO_V1(command_index_build_is_in_progress);

static ReIndexResult reindex_concurrently(Datum dbNameDatum,
										  Datum collectionNameDatum);
static IndexDef * ParseIndexDefDocument(const bson_iter_t *indexesArrayIter,
										bool ignoreUnknownIndexOptions);
static IndexDef * ParseIndexDefDocumentInternal(const bson_iter_t *indexesArrayIter,
												const char *indexSpecRepr,
												bool ignoreUnknownIndexOptions);
static void EnsureIndexDefDocFieldType(const bson_iter_t *indexDefDocIter,
									   bson_type_t expectedType);
static void EnsureIndexDefDocFieldConvertibleToBool(bson_iter_t *indexDefDocIter);
static bool IsSupportedIndexVersion(int indexVersion);
static void ThrowIndexDefDocMissingFieldError(const char *fieldName);
static IndexDefKey * ParseIndexDefKeyDocument(const bson_iter_t *indexDefDocIter);
static CosmosSearchOptions * ParseCosmosSearchOptionsDoc(const bson_iter_t *optsIter);
static BsonIntermediatePathNode * ParseIndexDefWildcardProjDoc(const bson_iter_t *
															   indexDefDocIter);
static BsonIntermediatePathNode * BuildBsonPathTreeForWPDocument(
	bson_iter_t *indexDefWPIter);
static void CheckWildcardProjectionTree(const BsonIntermediatePathNode *
										wildcardProjectionTree);
static WildcardProjFieldInclusionMode CheckWildcardProjectionTreeInternal(const
																		  BsonIntermediatePathNode
																		  *
																		  treeNode,
																		  bool
																		  isTopLevel,
																		  WildcardProjFieldInclusionMode
																		  expectedWPInclusionMode);
static const char * WPFieldInclusionModeString(WildcardProjFieldInclusionMode
											   wpInclusionMode);
static pgbson * GenerateWildcardProjDocument(const BsonIntermediatePathNode *
											 wildcardProjectionTree);
static pgbson * GenerateWildcardProjDocumentInternal(const
													 BsonIntermediatePathNode *treeNode,
													 bool isTopLevel);
static Expr * ParseIndexDefPartFilterDocument(const bson_iter_t *indexDefDocIter);
static bool CheckPartFilterExprOperatorsWalker(Node *node, void *context);
static void ThrowUnsupportedPartFilterExprError(Node *node);
static char * GetPartFilterExprNodeRepr(Node *node);
static bool GetPartFilterExprNodeReprWalker(Node *node, void *contextArg);
static bool CheckIndexSpecConflictWithExistingIndexes(uint64 collectionId,
													  const IndexSpec *indexSpec);
static void ThrowIndexNameConflictError(const IndexSpec *existingIndexSpec,
										const IndexSpec *requestedIndexSpec);
static void ThrowIndexOptionsConflictError(const char *existingIndexName);
static void ThrowSameIndexNameWithDifferentOptionsError(const IndexSpec *
														existingIndexSpec,
														const IndexSpec *
														requestedIndexSpec);
static void ThrowDifferentIndexNameWithDifferentOptionsError(const IndexSpec *
															 existingIndexSpec,
															 const IndexSpec *
															 requestedIndexSpec);
static void ThrowSingleTextIndexAllowedError(const IndexSpec *
											 existingIndexSpec,
											 const IndexSpec *
											 requestedIndexSpec);
static bool SetIndexesAsBuildInProgress(List *indexIdList, int *firstNotMarkedIndex);
static void UnsetIndexesAsBuildInProgress(List *indexIdList);
static LOCKTAG LockTagForInProgressIndexBuild(int indexId);
static TryCreateIndexesResult * TryCreateCollectionIndexes(uint64 collectionId,
														   List *indexDefList,
														   List *indexIdList,
														   bool isUnsharded,
														   MemoryContext retValueContext);
static void TryCreateInvalidCollectionIndexes(uint64 collectionId, List *indexDefList,
											  List *indexIdList, bool isUnsharded);
static TryReIndexesResult * TryReIndexCollectionIndexesConcurrently(uint64 collectionId,
																	List *
																	indexesDetailList,
																	List *indexIdList,
																	MemoryContext
																	retValueContext);
static void TryDropFailedCollectionIndexesAfterReIndex(uint64 collectionId,
													   IndexDetails *failedIndexd);
static void CreatePostgresIndex(uint64 collectionId, IndexDef *indexDef, int indexId,
								bool concurrently, bool isTempCollection,
								bool isUnsharded);
static void ReIndexPostgresIndex(uint64 collectionId, IndexDetails *indexDetail,
								 bool concurrently);
static WildcardProjectionPathOps * ResolveWPPathOpsFromTree(const
															BsonIntermediatePathNode *
															wildcardProjectionTree);
static void ResolveWPPathOpsFromTreeInternal(const BsonIntermediatePathNode *treeNode,
											 bool isTopLevel, List *pathFieldList,
											 List **nonIdFieldPathList,
											 WildcardProjFieldInclusionMode *
											 nonIdFieldInclusion,
											 WildcardProjFieldInclusionMode *
											 idFieldInclusion);
static char * GenerateIndexExprStr(bool unique, bool sparse, IndexDefKey *indexDefKey,
								   const BsonIntermediatePathNode *
								   indexDefWildcardProjTree,
								   const char *indexName, const char *defaultLanguage,
								   const char *languageOverride,
								   bool enableLargeIndexKeys);
static char * Generate2dsphereIndexExprStr(const IndexDefKey *indexDefKey);
static char * Generate2dsphereSparseExprStr(const IndexDefKey *indexDefKey);
static char * GenerateVectorIndexExprStr(IndexDefKey *indexDefKey,
										 const CosmosSearchOptions *searchOptions);
static char * GenerateIndexFilterStr(uint64 collectionId, Expr *indexDefPartFilterExpr);
static char * DeparseSimpleExprForDocument(uint64 collectionId, Expr *expr);
static void TryDropCollectionIndexes(uint64 collectionId, List *indexIdList,
									 List *indexIsUniqueList);
static pgbson * MakeReIndexMsg(ReIndexResult *result);
static void SendCreateIndexesResultToClientAsBson(FunctionCallInfo createIndexesFcinfo,
												  CreateIndexesResult *result,
												  DestReceiver *destReceiver);
static void SendReIndexResultToClientAsBson(FunctionCallInfo reIndexesFcinfo,
											ReIndexResult *result,
											DestReceiver *destReceiver);
static void ValidateIndexName(const bson_value_t *nameValue);

static BsonLeafPathNode * CreateIndexesCreateLeafNode(const StringView *fieldPath,
													  const char *relativePath,
													  void *state);
static const char * SerializeWeightedPaths(List *weightedPaths);


/*
 * IsCallCreateIndexesStmt returns true if given node is a CallStmt that runs
 * ApiSchema.create_indexes() UDF.
 */
bool
IsCallCreateIndexesStmt(const Node *node)
{
	if (!IsA(node, CallStmt))
	{
		return false;
	}

	return ((CallStmt *) node)->funcexpr->funcid == ApiCreateIndexesProcedureId();
}


/*
 * IsCallReIndexStmt returns true if given node is a CallStmt that runs
 * ApiSchema.re_index() UDF.
 */
bool
IsCallReIndexStmt(const Node *node)
{
	if (!IsA(node, CallStmt))
	{
		return false;
	}

	return ((CallStmt *) node)->funcexpr->funcid == ApiReIndexProcedureId();
}


/*
 * Compute index term limit based on configuration.
 */
inline static uint32_t
ComputeIndexTermLimit(uint32_t baseIndexTermLimit)
{
	uint32_t indexTermLimit = baseIndexTermLimit;

	if (IndexTruncationLimitOverride > 0 &&
		((uint32_t) IndexTruncationLimitOverride) < indexTermLimit)
	{
		indexTermLimit = IndexTruncationLimitOverride;
	}

	return indexTermLimit;
}


/*
 * command_create_indexes_non_concurrently is the implementation of the
 * internal logic of ApiInternalSchema.create_indexes_non_concurrently().
 */
Datum
command_create_indexes_non_concurrently(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("dbName cannot be NULL")));
	}
	Datum dbNameDatum = PG_GETARG_DATUM(0);

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("arg cannot be NULL")));
	}

	/* Temporary hack. This allows the MX Tests to call
	 * the non-concurrent index creation so that we can get
	 * tests to pass minus the stabilization of CREATE/DROP
	 * indexes. We will be able to ensure that in MX scenarios
	 * when Create/Drop indexes comes in, we'll be able to
	 * release MX with no further blockers. This is used for
	 * testing purposes only.
	 */
	bool skip_check_collection_create = false;

	/* For back-compat scenarios ensure that we check nargs */
	if (PG_NARGS() >= 3)
	{
		skip_check_collection_create = PG_GETARG_BOOL(2);
	}

	pgbson *arg = PgbsonDeduplicateFields(PG_GETARG_PGBSON(1));
	CreateIndexesArg createIndexesArg = ParseCreateIndexesArg(dbNameDatum,
															  arg);
	skip_check_collection_create |= createIndexesArg.blocking;
	bool uniqueIndexOnly = false;
	CreateIndexesResult result = create_indexes_non_concurrently(
		dbNameDatum, createIndexesArg, skip_check_collection_create, uniqueIndexOnly);

	PG_RETURN_POINTER(MakeCreateIndexesMsg(&result));
}


/*
 * command_create_temp_indexes_non_concurrently is the implementation of the
 * internal logic for creating indexes non concurrently in a temp collection from
 * an index definition message.
 *
 * Here, colletion is not a Mongo collection, and we don't perform any metata managemet
 * while creating the indexes (e.g., we don't record anything in ApiCatalogSchemaName.collection_indexes)
 */
Datum
command_create_temp_indexes_non_concurrently(PG_FUNCTION_ARGS)
{
	Datum dbNameDatum = PG_GETARG_DATUM(0);
	pgbson *createIndexesMessage = PgbsonDeduplicateFields(PG_GETARG_PGBSON(1));
	CreateIndexesArg createIndexesArg = ParseCreateIndexesArg(dbNameDatum,
															  createIndexesMessage);

	char *collectionName = createIndexesArg.collectionName;
	Datum collectionNameDatum = CStringGetTextDatum(collectionName);

	CreateIndexesResult result = {
		.ok = true /* we will throw an error otherwise */
	};

	MongoCollection *collection = GetTempMongoCollectionByNameDatum(dbNameDatum,
																	collectionNameDatum,
																	"documents_temp",
																	AccessShareLock);

	uint64 collectionId = collection->collectionId;
	bool isUnsharded = collection->shardKey == NULL;

	/* create indexes on data table and record them in metadata */
	ListCell *indexDefCell = NULL;
	int i = 0;
	foreach(indexDefCell, createIndexesArg.indexDefList)
	{
		IndexDef *indexDef = (IndexDef *) lfirst(indexDefCell);
		bool createIndexesConcurrently = false;
		bool isTempCollection = true;
		CreatePostgresIndex(collectionId, indexDef, i++, createIndexesConcurrently,
							isTempCollection, isUnsharded);
	}

	PG_RETURN_POINTER(MakeCreateIndexesMsg(&result));
}


/*
 * command_create_indexes is the implementation of the internal logic for
 * ApiSchema.create_indexes().
 */
void
command_create_indexes(const CallStmt *callStmt, ProcessUtilityContext context,
					   const ParamListInfo params, DestReceiver *destReceiver)
{
	/* must name it as "fcinfo" to be able to use PG_ARG/PG_GETARG functions */
	LOCAL_FCINFO(fcinfo, FUNC_MAX_ARGS);
	InitFCInfoForCallStmt(fcinfo, callStmt, context, params);

	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("dbName cannot be NULL")));
	}
	Datum dbNameDatum = PG_GETARG_DATUM(0);

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("arg cannot be NULL")));
	}

	/*
	 * Deduplicate elements of given bson object recursively so that we don't
	 * have multiple definitions for any bson field at any level.
	 *
	 * That way, we will not throw an error;
	 * - if we encounter with definition of a field more than once, or
	 * - if there is a syntax error in the prior definitions. e.g.:
	 *   {"createIndexes": 1, "createIndexes": "my_collection_name"}
	 * as Mongo does.
	 */
	pgbson *arg = PgbsonDeduplicateFields(PG_GETARG_PGBSON(1));
	CreateIndexesArg createIndexesArg = ParseCreateIndexesArg(dbNameDatum,
															  arg);
	bool isTopLevel = (context == PROCESS_UTILITY_TOPLEVEL);
	bool buildIndexesConcurrently = !IsInTransactionBlock(isTopLevel);
	buildIndexesConcurrently &= !createIndexesArg.blocking;
	bool skipCheckCollectionCreate = createIndexesArg.blocking;
	bool uniqueIndexOnly = false;
	CreateIndexesResult result = buildIndexesConcurrently ?
								 create_indexes_concurrently(dbNameDatum,
															 createIndexesArg,
															 uniqueIndexOnly) :
								 create_indexes_non_concurrently(dbNameDatum,
																 createIndexesArg,
																 skipCheckCollectionCreate,
																 uniqueIndexOnly);

	SendCreateIndexesResultToClientAsBson(fcinfo, &result, destReceiver);
}


/*
 * command_reindex is the implementation of the internal logic for
 * ApiSchema.re_index().
 */
void
command_reindex(const CallStmt *callStmt,
				ProcessUtilityContext context,
				const ParamListInfo params,
				DestReceiver *destReceiver)
{
	/* must name it as "fcinfo" to be able to use PG_ARG/PG_GETARG functions */
	LOCAL_FCINFO(fcinfo, FUNC_MAX_ARGS);
	InitFCInfoForCallStmt(fcinfo, callStmt, context, params);

	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("database name cannot be NULL")));
	}
	Datum dbNameDatum = PG_GETARG_DATUM(0);

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("collection name cannot be NULL")));
	}
	Datum collectionNameDatum = PG_GETARG_DATUM(1);

	bool isTopLevel = (context == PROCESS_UTILITY_TOPLEVEL);

	/*
	 * Mongo only supports reindex outside a transaction block
	 */
	if (IsInTransactionBlock(isTopLevel))
	{
		ereport(ERROR, (errcode(MongoOperationNotSupportedInTransaction),
						errmsg("Cannot run 'reIndex' in a multi-document transaction.")));
	}
	ReIndexResult result = reindex_concurrently(dbNameDatum, collectionNameDatum);

	SendReIndexResultToClientAsBson(fcinfo, &result, destReceiver);
}


/*
 * command_index_build_is_in_progress is the SQL interface for
 * IndexBuildIsInProgress.
 */
Datum
command_index_build_is_in_progress(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("indexId cannot be NULL")));
	}
	int indexId = DatumGetInt32(PG_GETARG_DATUM(0));

	PG_RETURN_BOOL(IndexBuildIsInProgress(indexId));
}


/*
 * create_indexes_concurrently is the internal function that creates
 * indexes concurrently.
 */
CreateIndexesResult
create_indexes_concurrently(Datum dbNameDatum, CreateIndexesArg createIndexesArg, bool
							uniqueIndexOnly)
{
	char *collectionName = createIndexesArg.collectionName;
	Datum collectionNameDatum = CStringGetTextDatum(collectionName);

	CreateIndexesResult result = { 0 };

	MongoCollection *collection =
		GetMongoCollectionByNameDatum(dbNameDatum, collectionNameDatum,
									  AccessShareLock);
	if (collection)
	{
		result.createdCollectionAutomatically = false;
	}
	else
	{
		/* collection does not exist, create it (or race for creating it) */
		result.createdCollectionAutomatically =
			CreateCollection(dbNameDatum, collectionNameDatum);

		collection = GetMongoCollectionByNameDatum(dbNameDatum, collectionNameDatum,
												   AccessShareLock);
	}

	uint64 collectionId = collection->collectionId;
	bool isUnsharded = collection->shardKey == NULL;
	AcquireAdvisoryExclusiveLockForCreateIndexes(collectionId);

	/*
	 * For both CheckForConflictsAndPruneExistingIndexes() and
	 * CollectionIdGetIndexCount(), we need to take built-in _id index
	 * (that we might have just created together with the collection itself)
	 * into the account too, so need to use a snapshot to which changes made
	 * by CreateCollection() are visible.
	 */
	PushActiveSnapshot(GetTransactionSnapshot());

	/*
	 * Prune away the indexes that already exist and throw an error if any
	 * of them would cause a name / option conflict.
	 *
	 * And before doing so, save the number of indexes that user actually
	 * wants to create to later determine whether we decided to not create
	 * some of them due to an identical index.
	 */
	int nindexesRequested = list_length(createIndexesArg.indexDefList);

	createIndexesArg.indexDefList =
		CheckForConflictsAndPruneExistingIndexes(collectionId,
												 createIndexesArg.indexDefList);

	result.numIndexesBefore = CollectionIdGetIndexCount(collectionId);

	if (result.numIndexesBefore + list_length(createIndexesArg.indexDefList) >
		MaxIndexesPerCollection)
	{
		int reportIndexDefIdx = MaxIndexesPerCollection - result.numIndexesBefore;
		const IndexDef *reportIndexDef = list_nth(createIndexesArg.indexDefList,
												  reportIndexDefIdx);
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg("add index fails, too many indexes for %s.%s key:%s",
							   collection->name.databaseName,
							   collection->name.collectionName,
							   PgbsonToJsonForLogging(reportIndexDef->keyDocument))));
	}

	/* pop the snapshot that we've just pushed above */
	PopActiveSnapshot();

	/*
	 * Record indexes into metadata as invalid.
	 *
	 * Also save uniqueness of the indexes since we might need to to call
	 * TryDropCollectionIndexes() later, in case of a failure.
	 */
	List *indexIdList = NIL;
	List *indexIsUniqueList = NIL;

	ListCell *indexDefCell = NULL;
	foreach(indexDefCell, createIndexesArg.indexDefList)
	{
		IndexDef *indexDef = (IndexDef *) lfirst(indexDefCell);
		if (uniqueIndexOnly && indexDef->unique != BoolIndexOption_True)
		{
			continue;
		}

		const IndexSpec indexSpec = MakeIndexSpecForIndexDef(indexDef);
		bool indexIsValid = false;
		int indexId = RecordCollectionIndex(collectionId, &indexSpec, indexIsValid);

		indexIdList = lappend_int(indexIdList, indexId);
		indexIsUniqueList = lappend_int(indexIsUniqueList,
										indexDef->unique == BoolIndexOption_True);
	}

	if (uniqueIndexOnly && indexIdList == NIL)
	{
		result.ok = true;
		result.numIndexesAfter = result.numIndexesBefore;
		return result;
	}

	/*
	 * Before the locks are released --due to commiting the transaction--, we
	 * mark our indexes as build-in-progress until UnsetIndexesAsBuildInProgress()
	 * is called, backend exits, or an ereport(ERROR) call is made.
	 *
	 * That way, other sessions querying index metadata can take our indexes
	 * into the account for various purposes.
	 *
	 * Note that we call UnsetIndexesAsBuildInProgress() only if index build is
	 * successful. This is because, TryCreateCollectionIndexes() only fails due
	 * a caught or uncought ereport(ERROR) call and that would anyway release the
	 * session level-lock that SetIndexesAsBuildInProgress() acquires, which has
	 * the same effect as calling UnsetIndexesAsBuildInProgress().
	 */
	int firstNotMarkedIndexId;
	if (!SetIndexesAsBuildInProgress(indexIdList, &firstNotMarkedIndexId))
	{
		ereport(ERROR, (errmsg("cannot mark index %d as build-in-progress",
							   firstNotMarkedIndexId)));
	}

	/* save the memory context before committing the transaction */
	MemoryContext procMemContext = CurrentMemoryContext;

	/* make entries persistent */
	PopAllActiveSnapshots();
	CommitTransactionCommand();
	StartTransactionCommand();

	TryCreateIndexesResult *tryResult =
		TryCreateCollectionIndexes(collectionId, createIndexesArg.indexDefList,
								   indexIdList, isUnsharded, procMemContext);

	if (!tryResult->ok)
	{
		result.ok = false;
		result.errcode = tryResult->errcode;
		result.errmsg = tryResult->errmsg;

		ereport(DEBUG1, (errmsg("trying to perform clean-up for any invalid "
								"indexes that might be left behind")));

		/*
		 * For each collection index that we know that we at least inserted
		 * an associated entry into index metadata, try to delete the metadata
		 * entry and drop the pg index associated with it (if created already).
		 *
		 * And right after doing that, here we commit the transaction to prevent
		 * any later ereport(ERROR) call from roll-backing the changes that we
		 * could have done for cleanup purposes.
		 *
		 * XXX: Normally, if the index creation failed for some reason after
		 *      creating the collection automatically, we should drop the
		 *      collection itself too. However, given that we commit the
		 *      transaction before creating indexes, the collection that we
		 *      created automatically would become visible to concurrent
		 *      transactions too. Moreover, they might have performed some
		 *      operations (e.g.: insert) on that collection. For this reason,
		 *      we cannot drop the collection here to avoid losing such changes
		 *      made concurrently.
		 *
		 *      Re-consider: Can we find a way to handle this case nicely ?
		 */
		TryDropCollectionIndexes(collectionId, indexIdList, indexIsUniqueList);

		PopAllActiveSnapshots();
		CommitTransactionCommand();
		StartTransactionCommand();
	}
	else
	{
		ereport(DEBUG1, (errmsg("created all collection indexes successfully")));

		UnsetIndexesAsBuildInProgress(indexIdList);

		result.ok = true;

		/*
		 * Set "note" field of the response message based on whether we
		 * rejected creating any indexes.
		 */
		if (list_length(createIndexesArg.indexDefList) == 0)
		{
			/*
			 * We don't allow "indexes" array to be empty, so this means that
			 * all the indexes already exist ..
			 */
			result.note = "all indexes already exist";
		}
		else if (list_length(createIndexesArg.indexDefList) < nindexesRequested)
		{
			/* then not all but some indexes already exist */
			result.note = "index already exists";
		}

		/*
		 * To be able to see updates made by MarkIndexesAsValid, need to
		 * use a snapshot to which those changes are visible.
		 */
		PushActiveSnapshot(GetTransactionSnapshot());
		result.numIndexesAfter = CollectionIdGetIndexCount(collectionId);
		PopActiveSnapshot();
	}

	return result;
}


/*
 * create_indexes_non_concurrently is the internal function that creates
 * indexes non-concurrently.
 */
CreateIndexesResult
create_indexes_non_concurrently(Datum dbNameDatum, CreateIndexesArg createIndexesArg,
								bool skipCheckCollectionCreate, bool uniqueIndexOnly)
{
	char *collectionName = createIndexesArg.collectionName;
	Datum collectionNameDatum = CStringGetTextDatum(collectionName);

	CreateIndexesResult result = {
		.ok = true /* we will throw an error otherwise */
	};

	MongoCollection *collection =
		GetMongoCollectionByNameDatum(dbNameDatum, collectionNameDatum,
									  AccessShareLock);
	if (collection)
	{
		result.createdCollectionAutomatically = false;
	}
	else
	{
		/* collection does not exist, create it (or race for creating it) */
		result.createdCollectionAutomatically =
			CreateCollection(dbNameDatum, collectionNameDatum);

		collection = GetMongoCollectionByNameDatum(dbNameDatum, collectionNameDatum,
												   AccessShareLock);
	}

	uint64 collectionId = collection->collectionId;
	bool isUnsharded = collection->shardKey == NULL;
	AcquireAdvisoryExclusiveLockForCreateIndexes(collectionId);

	/*
	 * Take built-in _id index that we might have just created into the
	 * account (e.g.: for CheckForConflictsAndPruneExistingIndexes()).
	 */
	PushActiveSnapshot(GetTransactionSnapshot());

	/*
	 * Prune away the indexes that already exist and throw an error if any
	 * of them would cause a name / option conflict.
	 *
	 * And before doing so, save the number of indexes that user actually
	 * wants to create to later determine whether we decided to not create
	 * some of them due to an identical index.
	 */
	int nindexesRequested = list_length(createIndexesArg.indexDefList);
	createIndexesArg.indexDefList =
		CheckForConflictsAndPruneExistingIndexes(collectionId,
												 createIndexesArg.indexDefList);

	result.numIndexesBefore = CollectionIdGetIndexCount(collectionId);

	if (result.numIndexesBefore + list_length(createIndexesArg.indexDefList) >
		MaxIndexesPerCollection)
	{
		int reportIndexDefIdx = MaxIndexesPerCollection - result.numIndexesBefore;
		const IndexDef *reportIndexDef = list_nth(createIndexesArg.indexDefList,
												  reportIndexDefIdx);
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg("add index fails, too many indexes for %s.%s key:%s",
							   collection->name.databaseName,
							   collection->name.collectionName,
							   PgbsonToJsonForLogging(reportIndexDef->keyDocument))));
	}

	/*
	 * Here we check when the table has been created, not the collection,
	 * since this allows creating indexes in non-concurrent mode for some
	 * internal callers such as shard_collection() as it doesn't create the
	 * collection from scratch but creates a new data table, and this would
	 * nearly mean the same thing in terms of concurrency semantics.
	 */
	if (!skipCheckCollectionCreate &&
		list_length(createIndexesArg.indexDefList) != 0 &&
		!IsDataTableCreatedWithinCurrentXact(collection))
	{
		ereport(ERROR, (errcode(MongoOperationNotSupportedInTransaction),
						errmsg("Cannot create new indexes on existing "
							   "collection %s.%s in a multi-document "
							   "transaction.",
							   collection->name.databaseName,
							   collection->name.collectionName)));
	}

	/* pop the snapshot that we've just pushed above */
	PopActiveSnapshot();

	/* create indexes on data table and record them in metadata */
	ListCell *indexDefCell = NULL;
	foreach(indexDefCell, createIndexesArg.indexDefList)
	{
		IndexDef *indexDef = (IndexDef *) lfirst(indexDefCell);
		if (uniqueIndexOnly && indexDef->unique != BoolIndexOption_True)
		{
			continue;
		}

		/*
		 * Record the index as valid since we will anyway rollback the
		 * transaction in case of an error.
		 */
		const IndexSpec indexSpec = MakeIndexSpecForIndexDef(indexDef);
		bool indexIsValid = true;
		int indexId = RecordCollectionIndex(collectionId, &indexSpec, indexIsValid);

		bool createIndexesConcurrently = false;
		bool isTempCollection = false;
		CreatePostgresIndex(collectionId, indexDef, indexId, createIndexesConcurrently,
							isTempCollection, isUnsharded);
	}

	/*
	 * Set "note" field of the response message based on whether we
	 * rejected creating any indexes.
	 */
	if (list_length(createIndexesArg.indexDefList) == 0)
	{
		/*
		 * We don't allow "indexes" array to be empty, so this means that
		 * all the indexes already exist ..
		 */
		result.note = "all indexes already exist";
	}
	else if (list_length(createIndexesArg.indexDefList) < nindexesRequested)
	{
		/* then not all but some indexes already exist */
		result.note = "index already exists";
	}

	/* to take indexes that we have just created into the account */
	PushActiveSnapshot(GetTransactionSnapshot());
	result.numIndexesAfter = CollectionIdGetIndexCount(collectionId);
	PopActiveSnapshot();

	return result;
}


/*
 * Rebuilds/reindexes all collection indexes concurrently
 */
static ReIndexResult
reindex_concurrently(Datum dbNameDatum, Datum collectionNameDatum)
{
	ReIndexResult result = { 0 };

	MongoCollection *collection =
		GetMongoCollectionByNameDatum(dbNameDatum, collectionNameDatum,
									  AccessShareLock);
	if (!collection)
	{
		ereport(ERROR, (errcode(MongoNamespaceNotFound),
						errmsg("collection does not exist.")));
	}

	uint64 collectionId = collection->collectionId;

	/*
	 * We wait until all the active createIndexes() operations release the
	 * AcquireAdvisoryExclusiveLockForCreateIndexes() lock, then only get the list of indexes to rebuild
	 *
	 * Please note that the lock will be released after first transaction commit, and further
	 * create index operation can run parallely, but those will not be considered for reIndex
	 */
	AcquireAdvisoryExclusiveLockForCreateIndexes(collectionId);

	bool excludeIdIndex = false;
	bool enableNestedDistribution = false;
	List *indexesDetailList = CollectionIdGetIndexes(collectionId, excludeIdIndex,
													 enableNestedDistribution);
	List *indexIdList = NIL;
	ListCell *indexDetailCell = NULL;
	foreach(indexDetailCell, indexesDetailList)
	{
		const IndexDetails *indexDetail = (IndexDetails *) lfirst(indexDetailCell);
		indexIdList = lappend_int(indexIdList, indexDetail->indexId);
	}

	/* save the memory context before committing the transaction */
	MemoryContext procMemContext = CurrentMemoryContext;

	/*
	 * Because we are rebuilding the indexes concurrently we should
	 * mark our indexes as build-in-progress until UnsetIndexesAsBuildInProgress()
	 * is called, backend exits, or an ereport(ERROR) call is made.
	 *
	 * That way, other sessions querying index metadata can take our indexes
	 * into the account for various purposes.
	 *
	 * If any of the index is still building then throw relevant mongo error
	 */
	int ignore;
	if (!SetIndexesAsBuildInProgress(indexIdList, &ignore))
	{
		ereport(ERROR, (errcode(MongoBackgroundOperationInProgressForNamespace),
						errmsg(
							"cannot perform operation: an index build is currently running for collection"
							" '%s.%s'", collection->name.databaseName,
							collection->name.collectionName)));
	}

	TryReIndexesResult *tryReIndexResult =
		TryReIndexCollectionIndexesConcurrently(collectionId, indexesDetailList,
												indexIdList,
												procMemContext);

	if (tryReIndexResult->ok)
	{
		ereport(DEBUG1, (errmsg(REINDEX_SUCCESSFUL_DEBUGMSG " concurrently")));

		/* Release the locks for index build in progress and mark them completed */
		UnsetIndexesAsBuildInProgress(indexIdList);

		result.ok = true;
		result.nIndexesWas = list_length(indexesDetailList);
		result.nIndexes = CollectionIdGetIndexCount(collectionId);
		result.indexesDetails = indexesDetailList;
	}
	else
	{
		result.ok = false;
		result.errcode = tryReIndexResult->errcode;
		result.errmsg = tryReIndexResult->errmsg;

		/*
		 * For any failed re-index, postgres might leave a transient index entry behind
		 * which is not used for query because it might not be complete but still has update overheads
		 *
		 * Try to drop any such invalid index as a result of failure
		 */
		TryDropFailedCollectionIndexesAfterReIndex(collectionId,
												   tryReIndexResult->
												   indexCurrentlyBuilding);
	}

	return result;
}


/*
 * InitFCInfoForCallStmt initializes given fcinfo by evaluating
 * expressions passed by given CallStmt and utility hook params.
 */
void
InitFCInfoForCallStmt(FunctionCallInfo fcinfo, const CallStmt *callStmt,
					  ProcessUtilityContext context, const ParamListInfo params)
{
	/* verify that function exists */
	const FuncExpr *funcexpr = callStmt->funcexpr;
	HeapTuple procTuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcexpr->funcid));
	if (!HeapTupleIsValid(procTuple))
	{
		ereport(ERROR, (errmsg("function with oid %u does not exist",
							   funcexpr->funcid)));
	}

	ReleaseSysCache(procTuple);

	/*
	 * Seems that we don't need to expand function arguments in pg >= 14, see
	 * pg commit e56bce5d43789cce95d099554ae9593ada92b3b7.
	 *
	 * Also, copy expr list in case executor functions scribble on Expr nodes
	 * we pass.
	 */
	List *funcexprArgs = copyObject(funcexpr->args);

	EState *estate = CreateExecutorState();
	estate->es_param_list_info = params;
	ExprContext *econtext = CreateExprContext(estate);

	int argIdx = 0;
	ListCell *expandedArgCell;
	foreach(expandedArgCell, funcexprArgs)
	{
		ExprState *exprstate = ExecPrepareExpr(lfirst(expandedArgCell), estate);

		/*
		 * Since we will shutdown estate soon, we should not switch to
		 * its per tuple memory context to evaluate params, so we use
		 * ExecEvalExpr here instead of ExecEvalExprSwitchContext.
		 */
		bool isnull = false;
		Datum val = ExecEvalExpr(exprstate, econtext, &isnull);
		fcinfo->args[argIdx].value = val;
		fcinfo->args[argIdx].isnull = isnull;

		argIdx++;
	}

	/* initialize everything except args array, which we've already done */
	FmgrInfo *flinfo = palloc0(sizeof(FmgrInfo));
	fmgr_info(funcexpr->funcid, flinfo);
	fmgr_info_set_expr((Node *) funcexpr, flinfo);
	CallContext *callcontext = makeNode(CallContext);
	callcontext->atomic = (!(context == PROCESS_UTILITY_TOPLEVEL ||
							 context == PROCESS_UTILITY_QUERY_NONATOMIC) ||
						   IsTransactionBlock());
	InitFunctionCallInfoData(*fcinfo, flinfo, list_length(funcexprArgs),
							 funcexpr->inputcollid, (Node *) callcontext, NULL);

	/* that anyway frees econtext too */
	FreeExecutorState(estate);
}


/*
 * ParseCreateIndexesArg returns a CreateIndexesArg object by parsing given
 * pgbson object that represents the "arg" document passed to
 * dbCommand/createIndexes.
 */
CreateIndexesArg
ParseCreateIndexesArg(Datum dbNameDatum, pgbson *arg)
{
	CreateIndexesArg createIndexesArg = { 0 };

	/*
	 * Distinguish "indexes: []" from not specifying "indexes" field at all,
	 * for a more suitable error message.
	 */
	bool gotIndexesArray = false;

	bson_iter_t argIter;
	PgbsonInitIterator(arg, &argIter);

	StringView ignoreUnknownIndexOptionsStr = {
		.string = "ignoreUnknownIndexOptions",
		.length = 25
	};
	createIndexesArg.blocking = false;
	createIndexesArg.ignoreUnknownIndexOptions = false;
	if (bson_iter_find_string_view(&argIter, &ignoreUnknownIndexOptionsStr))
	{
		if (EnsureTopLevelFieldIsBooleanLikeNullOk(
				"createIndexes.ignoreUnknownIndexOptions", &argIter))
		{
			createIndexesArg.ignoreUnknownIndexOptions = BsonValueAsBool(
				bson_iter_value(&argIter));
		}
	}

	PgbsonInitIterator(arg, &argIter);
	while (bson_iter_next(&argIter))
	{
		const char *argKey = bson_iter_key(&argIter);

		if (strcmp(argKey, "createIndexes") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(&argIter))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("collection name has invalid type %s",
									   BsonIterTypeName(&argIter))));
			}

			uint32_t strLength = 0;
			createIndexesArg.collectionName = pstrdup(bson_iter_utf8(&argIter,
																	 &strLength));

			if (strlen(createIndexesArg.collectionName) != (size_t) strLength)
			{
				ereport(ERROR, (errcode(MongoInvalidNamespace),
								errmsg(
									"namespaces cannot have embedded null characters")));
			}
		}
		else if (strcmp(argKey, "indexes") == 0)
		{
			EnsureTopLevelFieldType("createIndexes.indexes", &argIter, BSON_TYPE_ARRAY);

			gotIndexesArray = true;

			bson_iter_t indexesArrayIter;
			bson_iter_recurse(&argIter, &indexesArrayIter);
			while (bson_iter_next(&indexesArrayIter))
			{
				StringInfo fieldNameStr = makeStringInfo();
				int arrIdx = list_length(createIndexesArg.indexDefList);
				appendStringInfo(fieldNameStr, "createIndexes.indexes.%d", arrIdx);

				EnsureTopLevelFieldType(fieldNameStr->data, &indexesArrayIter,
										BSON_TYPE_DOCUMENT);

				createIndexesArg.indexDefList =
					lappend(createIndexesArg.indexDefList,
							ParseIndexDefDocument(&indexesArrayIter,
												  createIndexesArg.
												  ignoreUnknownIndexOptions));
			}
		}
		else if (strcmp(argKey, "ignoreUnknownIndexOptions") == 0)
		{
			/* ignore, already handled above */
		}
		else if (strcmp(argKey, "blocking") == 0)
		{
			if (EnsureTopLevelFieldIsBooleanLikeNullOk(
					"blocking", &argIter))
			{
				createIndexesArg.blocking = BsonValueAsBool(
					bson_iter_value(&argIter));
			}
		}
		else if (IsCommonSpecIgnoredField(argKey))
		{
			elog(DEBUG1, "Unrecognized command field: createIndexes.%s", argKey);

			/*
			 *  Silently ignore now, so that clients don't break
			 *  TODO: implement me
			 *      writeConcern
			 *      commitQuorum
			 *      comment
			 */
		}
		else
		{
			ereport(ERROR, (errcode(MongoUnknownBsonField),
							errmsg("BSON field 'createIndexes.%s' is an "
								   "unknown field", argKey)));
		}
	}

	/* verify that all non-optional fields are given */

	if (!gotIndexesArray)
	{
		ThrowTopLevelMissingFieldError("createIndexes.indexes");
	}

	if (createIndexesArg.collectionName == NULL ||
		createIndexesArg.collectionName[0] == '\0')
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg("Invalid namespace specified '%s.'",
							   TextDatumGetCString(dbNameDatum))));
	}

	if (list_length(createIndexesArg.indexDefList) == 0)
	{
		/* "indexes" field is specified, but to be an empty array */
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Must specify at least one index to create")));
	}

	return createIndexesArg;
}


/*
 * ParseIndexDefDocument is a wrapper around ParseIndexDefDocumentInternal
 * that extends error messages related with Mongo errors that might be thrown
 * when parsing "indexes" field of "arg" document passed to
 * dbCommand/createIndexes.
 */
static IndexDef *
ParseIndexDefDocument(const bson_iter_t *indexesArrayIter, bool ignoreUnknownIndexOptions)
{
	const char *indexSpecRepr = PgbsonIterDocumentToJsonForLogging(indexesArrayIter);
	StringInfo errorMessagePrefixStr = makeStringInfo();
	appendStringInfo(errorMessagePrefixStr,
					 "Error in specification %s :: caused by :: ",
					 indexSpecRepr);

	MemoryContext savedMemoryContext = CurrentMemoryContext;
	IndexDef *indexDef = NULL;
	PG_TRY();
	{
		indexDef = ParseIndexDefDocumentInternal(indexesArrayIter, indexSpecRepr,
												 ignoreUnknownIndexOptions);
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(savedMemoryContext);
		RethrowPrependMongoError(errorMessagePrefixStr->data);
	}
	PG_END_TRY();

	return indexDef;
}


/*
 * ParseIndexDefDocumentInternal returns an IndexDef object by parsing value
 * of pgbson iterator that points to the "indexes" field of "arg" document
 * passed to dbCommand/createIndexes.
 */
static IndexDef *
ParseIndexDefDocumentInternal(const bson_iter_t *indexesArrayIter,
							  const char *indexSpecRepr,
							  bool ignoreUnknownIndexOptions)
{
	/*
	 * Distinguish "key: {}" from not specifying "key" field at all,
	 * for a more suitable error message.
	 */
	bool gotKeyDocument = false;
	bool isTTLIndex = false;

	IndexDef *indexDef = palloc0(sizeof(IndexDef));

	/* set to default value in case it's unset */
	indexDef->version = 2;

	/* Set to 0 to denote sphere index not present */
	indexDef->sphereIndexVersion = 0;

	bson_iter_t indexDefDocIter;
	bson_iter_recurse(indexesArrayIter, &indexDefDocIter);
	while (bson_iter_next(&indexDefDocIter))
	{
		const char *indexDefDocKey = bson_iter_key(&indexDefDocIter);
		if (strcmp(indexDefDocKey, "key") == 0)
		{
			EnsureIndexDefDocFieldType(&indexDefDocIter,
									   BSON_TYPE_DOCUMENT);

			gotKeyDocument = true;

			indexDef->key = ParseIndexDefKeyDocument(&indexDefDocIter);
			indexDef->keyDocument = PgbsonInitFromIterDocumentValue(&indexDefDocIter);
		}
		else if (strcmp(indexDefDocKey, "wildcardProjection") == 0)
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg("The field 'wildcardProjection' must be "
									   "a non-empty object, but got %s",
									   BsonIterTypeName(&indexDefDocIter))));
			}

			indexDef->wildcardProjectionTree =
				ParseIndexDefWildcardProjDoc(&indexDefDocIter);
		}
		else if (strcmp(indexDefDocKey, "name") == 0)
		{
			EnsureIndexDefDocFieldType(&indexDefDocIter,
									   BSON_TYPE_UTF8);

			const bson_value_t *nameVal = bson_iter_value(&indexDefDocIter);
			ValidateIndexName(nameVal);
			indexDef->name = pstrdup(nameVal->value.v_utf8.str);
		}
		else if (strcmp(indexDefDocKey, "unique") == 0)
		{
			ReportFeatureUsage(FEATURE_CREATE_INDEX_UNIQUE);
			EnsureIndexDefDocFieldConvertibleToBool(&indexDefDocIter);

			bool uniqueVal = bson_iter_as_bool(&indexDefDocIter);
			indexDef->unique = uniqueVal ? BoolIndexOption_True
							   : BoolIndexOption_False;
		}
		else if (strcmp(indexDefDocKey, "partialFilterExpression") == 0)
		{
			EnsureIndexDefDocFieldType(&indexDefDocIter,
									   BSON_TYPE_DOCUMENT);

			indexDef->partialFilterExpr =
				ParseIndexDefPartFilterDocument(&indexDefDocIter);
			indexDef->partialFilterExprDocument =
				PgbsonInitFromIterDocumentValue(&indexDefDocIter);
		}
		else if (strcmp(indexDefDocKey, "sparse") == 0)
		{
			EnsureIndexDefDocFieldConvertibleToBool(&indexDefDocIter);

			const bool sparseVal = bson_iter_as_bool(&indexDefDocIter);
			indexDef->sparse = sparseVal ? BoolIndexOption_True :
							   BoolIndexOption_False;
		}
		else if (strcmp(indexDefDocKey, "v") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg("The field 'v' must be a number, but got %s",
									   BsonIterTypeName(&indexDefDocIter))));
			}

			double vValAsDouble = BsonValueAsDouble(bson_iter_value(&indexDefDocIter));
			int vValAsInt = (int) vValAsDouble;
			if (vValAsDouble < INT_MIN || vValAsDouble > INT_MAX ||
				vValAsInt != vValAsDouble)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("Index version must be representable as a "
									   "32-bit integer, but got %lf",
									   vValAsDouble)));
			}
			else if (!IsSupportedIndexVersion(vValAsInt))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("Invalid index specification %s; cannot "
									   "create an index with v=%d",
									   indexSpecRepr, vValAsInt)));
			}

			indexDef->version = vValAsInt;
		}
		else if (strcmp(indexDefDocKey, "expireAfterSeconds") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"TTL index 'expireAfterSeconds' option must be numeric, but received a type of %s.",
									BsonIterTypeName(&indexDefDocIter))));
			}

			/* TTL Index */
			double expireAfterSecondsValAsDouble = BsonValueAsDouble(bson_iter_value(
																		 &indexDefDocIter));
			int expireAfterSecondsValAsInt = (int) expireAfterSecondsValAsDouble;
			if (expireAfterSecondsValAsDouble < INT_MIN ||
				expireAfterSecondsValAsDouble > INT_MAX ||
				expireAfterSecondsValAsInt != expireAfterSecondsValAsDouble)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"TTL index 'expireAfterSeconds' option must be within an acceptable range, try a different number than %lf.",
									expireAfterSecondsValAsDouble)));
			}
			else if (expireAfterSecondsValAsInt < 0)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"TTL index 'expireAfterSeconds' option cannot be less than 0.")));
			}

			indexDef->expireAfterSeconds = palloc0(sizeof(int));
			*(indexDef->expireAfterSeconds) = expireAfterSecondsValAsInt;
			isTTLIndex = true;
		}
		else if (strcmp(indexDefDocKey, "default_language") == 0)
		{
			EnsureIndexDefDocFieldType(&indexDefDocIter,
									   BSON_TYPE_UTF8);

			const bson_value_t *languageValue = bson_iter_value(&indexDefDocIter);
			indexDef->defaultLanguage = pstrdup(languageValue->value.v_utf8.str);
		}
		else if (strcmp(indexDefDocKey, "language_override") == 0)
		{
			EnsureIndexDefDocFieldType(&indexDefDocIter,
									   BSON_TYPE_UTF8);

			const bson_value_t *languageOveride = bson_iter_value(&indexDefDocIter);
			indexDef->languageOverride = pstrdup(languageOveride->value.v_utf8.str);
		}
		else if (strcmp(indexDefDocKey, "weights") == 0)
		{
			/* Special case, mongo allows "weight": "$**" */
			if (bson_iter_type(&indexDefDocIter) == BSON_TYPE_UTF8 &&
				strcmp(bson_iter_utf8(&indexDefDocIter, NULL), "$**") == 0)
			{
				pgbson_writer writer;
				PgbsonWriterInit(&writer);
				PgbsonWriterAppendDouble(&writer, "$**", 3, 1);
				indexDef->weightsDocument = PgbsonWriterGetPgbson(&writer);
			}
			else
			{
				EnsureIndexDefDocFieldType(&indexDefDocIter,
										   BSON_TYPE_DOCUMENT);
				indexDef->weightsDocument = PgbsonInitFromIterDocumentValue(
					&indexDefDocIter);
			}
		}
		else if (strcmp(indexDefDocKey, "textIndexVersion") == 0)
		{
			/* We only support version 2 (Diacritic sensitive, case insensitive) */
			const bson_value_t *value = bson_iter_value(&indexDefDocIter);
			if (!BsonValueIsNumber(value))
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg(
									"The field 'textIndexVersion' must be a number, but got %s",
									BsonTypeName(value->value_type))));
			}

			int version = BsonValueAsInt32(value);
			if (version != 2)
			{
				ereport(ERROR, (errcode(MongoCommandNotSupported),
								errmsg(
									"Currently only textIndexVersion 2 is supported, not %d",
									version),
								errhint(
									"Currently only textIndexVersion 2 is supported, not %d",
									version)));
			}
		}
		else if (strcmp(indexDefDocKey, "background") == 0)
		{
			/* This is deprecated but old drivers can call it - ignore */
			continue;
		}
		else if (strcmp(indexDefDocKey, "ns") == 0)
		{
			/* This is ignored by Mongodb */
			continue;
		}
		else if (strcmp(indexDefDocKey, "cosmosSearchOptions") == 0)
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"The field cosmosSearch must be a document. got '%s'",
									BsonTypeName(bson_iter_type(&indexDefDocIter)))));
			}

			indexDef->cosmosSearchOptions =
				ParseCosmosSearchOptionsDoc(&indexDefDocIter);

			indexDef->cosmosSearchOptions->searchOptionsDoc =
				BsonValueToDocumentPgbson(bson_iter_value(&indexDefDocIter));
		}
		else if (strcmp(indexDefDocKey, "hidden") == 0)
		{
			if (!BSON_ITER_HOLDS_BOOL(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("The field hidden must be a bool. got '%s'",
									   BsonTypeName(bson_iter_type(&indexDefDocIter)))));
			}

			bool hidden = bson_iter_bool(&indexDefDocIter);
			if (hidden)
			{
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("hidden indexes are not supported yet.")));
			}
		}
		else if (strcmp(indexDefDocKey, "dropDups") == 0)
		{
			/* dropDups is deprecated and is supposed to be ignored */
			continue;
		}
		else if (strcmp(indexDefDocKey, "max") == 0)
		{
			/* Optional for 2d index */
			if (!BSON_ITER_HOLDS_NUMBER(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg(
									"The field 'max' must be a number, but got %s.",
									BsonIterTypeName(&indexDefDocIter))));
			}

			/* max bound */
			double maxBound = BsonValueAsDoubleQuiet(bson_iter_value(&indexDefDocIter));
			indexDef->maxBound = palloc0(sizeof(double));
			*(indexDef->maxBound) = maxBound;
		}
		else if (strcmp(indexDefDocKey, "min") == 0)
		{
			/* Optional for 2d index */
			if (!BSON_ITER_HOLDS_NUMBER(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg(
									"The field 'min' must be a number, but got %s.",
									BsonIterTypeName(&indexDefDocIter))));
			}

			/* min bound*/
			double minBound = BsonValueAsDoubleQuiet(bson_iter_value(&indexDefDocIter));
			indexDef->minBound = palloc0(sizeof(double));
			*(indexDef->minBound) = minBound;
		}
		else if (strcmp(indexDefDocKey, "bits") == 0)
		{
			/* Optional for 2d index */
			if (!BSON_ITER_HOLDS_NUMBER(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg(
									"The field 'bits' must be a number, but got %s.",
									BsonIterTypeName(&indexDefDocIter))));
			}

			int32_t bits = BsonValueAsInt32(bson_iter_value(&indexDefDocIter));

			if (bits < 1 || bits > 32)
			{
				ereport(ERROR, (errcode(MongoInvalidOptions),
								errmsg(
									"bits for hash must be > 0 and <= 32, but %d bits were specified",
									bits)));
			}

			indexDef->bits = bits;
		}
		else if (strcmp(indexDefDocKey, "2dsphereIndexVersion") == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&indexDefDocIter))
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg(
									"The field '2dsphereIndexVersion' must be a number, but got %s.",
									BsonIterTypeName(&indexDefDocIter))));
			}
			int32_t sphereIndexVersion = BsonValueAsInt32(bson_iter_value(
															  &indexDefDocIter));
			if (sphereIndexVersion != 3)
			{
				/* Mongo supports [1, 2, 3] version for 2dsphere index, but we only support the latest which is 3*/
				ereport(ERROR, (
							errcode(MongoCannotCreateIndex),
							errmsg("unsupported geo index version found, "
								   "only versions: [3] is supported")));
			}
		}
		/*
		 * These are non documented Mongo 2dsphere index options
		 * We don't know what these are used for but JStest jstests\core\geo_s2index.js
		 * validates this, so we just parse it for validity and then ignore these
		 */
		else if (strcmp(indexDefDocKey, "finestIndexedLevel") == 0)
		{
			EnsureIndexDefDocFieldConvertibleToBool(&indexDefDocIter);
			const bson_value_t *value = bson_iter_value(&indexDefDocIter);
			bool checkFixed = true;
			if (!IsBsonValue32BitInteger(value, checkFixed))
			{
				ereport(ERROR, (
							errcode(MongoInvalidOptions),
							errmsg(
								"Expected field \"finestIndexedLevel\" to have a value exactly representable "
								"as a 64-bit integer, but found finestIndexedLevel: %s",
								BsonValueToJsonForLogging(value)),
							errhint(
								"Expected field \"finestIndexedLevel\" to have a value exactly representable "
								"as a 64-bit integer")));
			}
			indexDef->finestIndexedLevel = palloc0(sizeof(int32_t));
			*(indexDef->finestIndexedLevel) = BsonValueAsInt32(bson_iter_value(
																   &indexDefDocIter));
		}
		else if (strcmp(indexDefDocKey, "coarsestIndexedLevel") == 0)
		{
			EnsureIndexDefDocFieldConvertibleToBool(&indexDefDocIter);
			const bson_value_t *value = bson_iter_value(&indexDefDocIter);
			bool checkFixed = true;
			if (!IsBsonValue32BitInteger(value, checkFixed))
			{
				ereport(ERROR, (
							errcode(MongoInvalidOptions),
							errmsg(
								"Expected field \"coarsestIndexedLevel\" to have a value exactly representable "
								"as a 64-bit integer, but found coarsestIndexedLevel: %s",
								BsonValueToJsonForLogging(value)),
							errhint(
								"Expected field \"coarsestIndexedLevel\" to have a value exactly representable "
								"as a 64-bit integer")));
			}
			indexDef->coarsestIndexedLevel = palloc0(sizeof(int32_t));
			*(indexDef->coarsestIndexedLevel) = BsonValueAsInt32(bson_iter_value(
																	 &indexDefDocIter));
		}
		else if (strcmp(indexDefDocKey, "enableLargeIndexKeys") == 0)
		{
			const bson_value_t *value = bson_iter_value(&indexDefDocIter);
			if (BsonValueAsBool(value))
			{
				indexDef->enableLargeIndexKeys = BoolIndexOption_True;
			}
			else
			{
				indexDef->enableLargeIndexKeys = BoolIndexOption_False;
			}
		}
		else if (!ignoreUnknownIndexOptions)
		{
			/*
			 * TODO: Should also handle options in separate conditional blocks.
			 *       When the key doesn't correspond any of those, then we would
			 *       error here for the unexpected field.
			 */
			ereport(ERROR, (errcode(MongoInvalidIndexSpecificationOption),
							errmsg("The field '%s' is not valid for an index "
								   "specification. Specification: %s",
								   indexDefDocKey,
								   PgbsonIterDocumentToJsonForLogging(
									   indexesArrayIter))));
		}
	}

	/* verify that all non-optional fields are given */

	if (!gotKeyDocument)
	{
		ThrowIndexDefDocMissingFieldError("key");
	}

	if (!indexDef->key->isWildcard)
	{
		if (list_length(indexDef->key->keyPathList) == 0)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg("Index keys cannot be an empty field")));
		}
		else if (indexDef->wildcardProjectionTree)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("The field 'wildcardProjection' is only "
								   "allowed in an 'wildcard' index")));
		}
	}
	else
	{
		if (indexDef->wildcardProjectionTree)
		{
			/*
			 * If dummy root doesn't have any children, then it means that
			 * "wildcardProjection" document is empty.
			 */
			if (!IntermediateNodeHasChildren(indexDef->wildcardProjectionTree))
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg("The 'wildcardProjection' field can't be an "
									   "empty object")));
			}
			else if (list_length(indexDef->key->keyPathList) != 0)
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg("The field 'wildcardProjection' is only "
									   "allowed when 'key' is {\"$**\": ±1}")));
			}
			else if (indexDef->key->hasTextIndexes)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"The field 'wildcardProjection' is only allowed in an 'wildcard' index")));
			}

			/*
			 * All validations for wildcardProjection have been performed, now
			 * we can generate the bson document for it.
			 */
			indexDef->wildcardProjectionDocument =
				GenerateWildcardProjDocument(indexDef->wildcardProjectionTree);
		}
	}

	if (indexDef->name == NULL)
	{
		ThrowIndexDefDocMissingFieldError("name");
	}

	if (indexDef->enableLargeIndexKeys == BoolIndexOption_True)
	{
		if (indexDef->key->isWildcard || indexDef->wildcardProjectionDocument != NULL)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"enableLargeIndexKeys not supported with wildcard indexes.")));
		}
		if (indexDef->key->hasHashedIndexes ||
			indexDef->key->hasTextIndexes ||
			indexDef->key->has2dIndex ||
			indexDef->key->has2dsphereIndex ||
			indexDef->key->hasCosmosIndexes)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"enableLargeIndexKeys is only supported with regular indexes.")));
		}

		if (indexDef->expireAfterSeconds != NULL)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"enableLargeIndexKeys not supported with TTL indexes.")));
		}

		if (indexDef->unique != BoolIndexOption_Undefined)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"Cannot specify unique with enableLargeIndexKeys.")));
		}
	}

	if (indexDef->key->hasCosmosIndexes &&
		indexDef->cosmosSearchOptions == NULL)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"Index type 'CosmosSearch' was requested, but the 'cosmosSearch' options were not provided.")));
	}

	if (!indexDef->key->hasTextIndexes &&
		indexDef->defaultLanguage != NULL)
	{
		/* Today, default_language is only supported for text indexes */
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg(
							"default_language can only be specified for text indexes currently.")));
	}

	if (!indexDef->key->hasTextIndexes &&
		indexDef->languageOverride != NULL)
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"language_override can only be specified for text indexes.")));
	}

	if (indexDef->unique == BoolIndexOption_True && indexDef->key->isWildcard)
	{
		ereport(ERROR, errcode(MongoFailedToParse),
				errmsg("Index type 'wildcard' does not support the unique option"));
	}

	if (indexDef->unique == BoolIndexOption_True && indexDef->key->hasHashedIndexes)
	{
		ereport(ERROR, errcode(MongoLocation16764),
				errmsg(
					"Currently hashed indexes cannot guarantee uniqueness. Use a regular index"));
	}

	if (indexDef->unique == BoolIndexOption_True && indexDef->key->hasTextIndexes)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg(
							"Index type 'text' does not support the unique option")));
	}

	if (indexDef->unique == BoolIndexOption_True && indexDef->enableLargeIndexKeys ==
		BoolIndexOption_True)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						errmsg(
							"enableLargeIndexKeys does not support the unique option")));
	}

	if (indexDef->key->hasCosmosIndexes)
	{
		if (indexDef->unique == BoolIndexOption_True)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"Index type 'cosmosSearch' does not support the unique option")));
		}

		if (indexDef->partialFilterExprDocument != NULL)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"Index type 'cosmosSearch' does not support the partial filters")));
		}
	}

	if (indexDef->key->has2dIndex)
	{
		if (indexDef->unique == BoolIndexOption_True)
		{
			/* TODO: Mongo supports the unique option with 2d index, but we dont know how to support
			 * unique indexes with GIST
			 */
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"Index type '2d' does not support the unique option")));
		}

		if (list_length(indexDef->key->keyPathList) > 1)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("Compound 2d indexes are not supported yet")));
		}

		double minBound = indexDef->minBound ? *indexDef->minBound :
						  DEFAULT_2D_INDEX_MIN_BOUND;
		double maxBound = indexDef->maxBound ? *indexDef->maxBound :
						  DEFAULT_2D_INDEX_MAX_BOUND;

		if (isinf(minBound) || isinf(maxBound) || maxBound <= minBound)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"region for hash must be valid and have positive area, but [%g, %g] was specified",
								minBound, maxBound)));
		}
	}

	if (indexDef->key->has2dsphereIndex)
	{
		if (indexDef->unique == BoolIndexOption_True)
		{
			/* TODO: Mongo supports the unique option with 2dsphere index, but we dont how to support
			 * unique indexes with GIST
			 */
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"Index type '2dsphere' does not support the unique option")));
		}

		int32_t coarsest = indexDef->coarsestIndexedLevel ?
						   *(indexDef->coarsestIndexedLevel) : 0;
		int32_t finest = indexDef->finestIndexedLevel ? *(indexDef->finestIndexedLevel) :
						 30;
		if (coarsest < 0)
		{
			ereport(ERROR, (
						errcode(MongoLocation16747),
						errmsg("coarsestIndexedLevel must be >= 0")));
		}

		if (finest > 30)
		{
			ereport(ERROR, (
						errcode(MongoLocation16748),
						errmsg("finestIndexedLevel must be <= 30")));
		}

		if (coarsest > finest)
		{
			ereport(ERROR, (
						errcode(MongoLocation16749),
						errmsg("finestIndexedLevel must be >= coarsestIndexedLevel")));
		}

		indexDef->sphereIndexVersion = 3;
	}

	if (indexDef->sparse == BoolIndexOption_True)
	{
		if (indexDef->key->isWildcard)
		{
			ereport(ERROR, (errcode(MongoInvalidIndexSpecificationOption),
							errmsg(
								"Index type 'wildcard' does not support the sparse option")));
		}

		if (indexDef->partialFilterExpr != NULL)
		{
			ereport(ERROR, (errcode(MongoInvalidIndexSpecificationOption),
							errmsg(
								"cannot mix \"partialFilterExpression\" and \"sparse\" options")));
		}
	}

	if (indexDef->weightsDocument != NULL && !indexDef->key->hasTextIndexes)
	{
		ereport(ERROR, (errcode(MongoInvalidIndexSpecificationOption),
						errmsg(
							"the field 'weights' can only be specified with text indexes")));

		bson_iter_t weightsIterator;
		PgbsonInitIterator(indexDef->weightsDocument, &weightsIterator);
	}

	if (indexDef->weightsDocument != NULL)
	{
		/* Wildcard for text can come via weights */
		bool isTextWildcard = false;
		bson_value_t docValue = ConvertPgbsonToBsonValue(indexDef->weightsDocument);
		indexDef->key->textPathList = MergeTextIndexWeights(indexDef->key->textPathList,
															&docValue, &isTextWildcard);
		if (isTextWildcard)
		{
			/* Find the text index */
			if (list_length(indexDef->key->keyPathList) == 0)
			{
				indexDef->key->isWildcard = true;
			}
			else
			{
				ListCell *cell;
				foreach(cell, indexDef->key->keyPathList)
				{
					IndexDefKeyPath *keyPath = lfirst(cell);
					if (keyPath->indexKind == MongoIndexKind_Text)
					{
						keyPath->isWildcard = true;
					}
				}
			}
		}
	}

	/*
	 * Below are the check we peform on TTL index spec
	 *  1. TTL index is not allowed on composite keys. TTL index needs to be single field.
	 *  2. TTL index can't be defined on _id. If the spec contains multiple _id fields and nothing else
	 * it is considered a single field spec to match mongo error reporting.
	 *  3. TTL index can't be a wildcard index
	 *
	 * FYI: 1. Unique and Sparse are valid options for ttl index
	 *       2. TTL index can be of type hash
	 */

	if (isTTLIndex)
	{
		ReportFeatureUsage(FEATURE_CREATE_INDEX_TTL);

		ListCell *keyPathCell = NULL;
		int totalIndexKeyPath = 0;
		int totalIdKeyPath = 0;
		foreach(keyPathCell, indexDef->key->keyPathList)
		{
			IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) lfirst(keyPathCell);
			char *keyPath = (char *) indexKeyPath->path;

			if (strcmp(keyPath, "_id") == 0)
			{
				totalIdKeyPath++;
			}

			totalIndexKeyPath++;
		}

		/* "key" : { "_id" : 1, "_id" : 1 }. Native Mongo error sees this spec as a single-field spec on _id. */
		if (totalIdKeyPath == totalIndexKeyPath)
		{
			ereport(ERROR, (errcode(MongoInvalidIndexSpecificationOption),
							errmsg(
								"The field 'expireAfterSeconds' is not valid for an _id index specification.")));
		}

		if (totalIndexKeyPath > 1)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"TTL indexes are single-field indexes, compound indexes do not support TTL.")));
		}

		if (indexDef->key->isWildcard)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"Index type 'wildcard' cannot be a TTL index.")));
		}

		/*
		 *  Native Mongo supports creating a ttl index as hash index. We can support this too but it is not
		 *  performant based on our current approch. If the ttl index is a hash index - searching for all
		 *  the expired documents would require a scan, as range queries are not supported on hash indexes.
		 *
		 *  It is possible to create a parallel b-tree based index - that way we can use the b-tree index to search
		 *  for the expired documents while supporting a ttl hash index. At some point, if we decide to support
		 *  "ttl hash indexes" - this may be one of the approaches to try.
		 *
		 *  In fact, Mongo's official documentation suggests a similar approch for getting around uniqueness guarantee
		 *  issues related to hash indexes. They suggest creating a parallel non-hashed index to enforce uniqueness,
		 *  if one needs a hash index with unique keys.
		 */
		if (indexDef->key->hasHashedIndexes)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Creating a hash index as ttl index is not supported.")));
		}

		if (indexDef->key->hasTextIndexes)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Creating a text index as ttl index is not supported.")));
		}

		if (indexDef->key->hasCosmosIndexes)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Creating a cosmosSearch index as ttl index is not supported.")));
		}

		if (indexDef->key->has2dIndex)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Creating a 2d index as ttl index is not supported.")));
		}

		if (indexDef->key->has2dsphereIndex)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Creating a 2dsphere index as ttl index is not supported.")));
		}
	}

	return indexDef;
}


/*
 * EnsureIndexDefDocFieldType throws a MongoTypeMismatch error for given
 * field that is known to be a part of index definition document that
 * indexesArrayIter holds.
 */
static void
EnsureIndexDefDocFieldType(const bson_iter_t *indexDefDocIter,
						   bson_type_t expectedType)
{
	bson_type_t bsonType = bson_iter_type(indexDefDocIter);
	if (bsonType != expectedType)
	{
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg("The field '%s' must be an %s, but got %s",
							   bson_iter_key(indexDefDocIter), BsonTypeName(expectedType),
							   BsonTypeName(bsonType))));
	}
}


/*
 * EnsureIndexDefDocFieldConvertibleToBool throws a MongoTypeMismatch error for given
 * field that is known to accept a number (represented as bool) or bool.
 */
static void
EnsureIndexDefDocFieldConvertibleToBool(bson_iter_t *indexDefDocIter)
{
	if (!BSON_ITER_HOLDS_BOOL(indexDefDocIter) &&
		!BSON_ITER_HOLDS_NUMBER(indexDefDocIter))
	{
		const bson_value_t *value = bson_iter_value(indexDefDocIter);
		const char *name = bson_iter_key(indexDefDocIter);
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg(
							"The field '%s' has value %s: %s, which is not convertible to bool",
							name, name, BsonValueToJsonForLogging(value)),
						errhint(
							"Field in index definition has value of type %s, which is not convertible to bool",
							BsonTypeName(value->value_type))));
	}
}


/*
 * IsSupportedIndexVersion return true if given index version ("v") is supported.
 */
static bool
IsSupportedIndexVersion(int indexVersion)
{
	switch (indexVersion)
	{
		case 1:
		case 2:
		{
			return true;
		}

		default:
			return false;
	}
}


/*
 * ThrowIndexDefDocMissingFieldError throws a MongoFailedToParse error for given
 * field that is known to be a part of index definition document that
 * indexesArrayIter holds.
 */
static void
ThrowIndexDefDocMissingFieldError(const char *fieldName)
{
	ereport(ERROR, (errcode(MongoFailedToParse),
					errmsg("The '%s' field is a required property of "
						   "an index specification", fieldName)));
}


/*
 * ParseIndexDefKeyDocument returns an IndexDefKey object by parsing value of
 * pgbson iterator that points to the "key" field of an index definiton
 * document.
 */
static IndexDefKey *
ParseIndexDefKeyDocument(const bson_iter_t *indexDefDocIter)
{
	IndexDefKey *indexDefKey = palloc0(sizeof(IndexDefKey));

	int numHashedIndexes = 0;
	MongoIndexKind allindexKinds = MongoIndexKind_Unknown;
	MongoIndexKind lastIndexKind = MongoIndexKind_Unknown;
	MongoIndexKind wildcardIndexKind = 0;

	bson_iter_t indexDefKeyIter;
	bson_iter_recurse(indexDefDocIter, &indexDefKeyIter);
	while (bson_iter_next(&indexDefKeyIter))
	{
		if (indexDefKey->isWildcard && (wildcardIndexKind & ~MongoIndexKind_Text) != 0)
		{
			/*
			 * Already parsed a wildcard keyPath before and got another
			 * keyPath now. However, compound indexes cannot contain a
			 * wildcard key.
			 * TODO: Support compound wildcard *iff* it's a root text index.
			 */
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg("wildcard indexes do not allow compounding")));
		}

		const char *indexDefKeyKey = bson_iter_key(&indexDefKeyIter);

		bool isWildcardKeyPath = false;
		bool wildcardOnWholeDocument =
			(strcmp(indexDefKeyKey, WILDCARD_INDEX_SUFFIX) == 0);
		if (wildcardOnWholeDocument)
		{
			/* wildcard index on whole document */
			isWildcardKeyPath = true;
		}
		else
		{
			/* Case 1: Index key field should not start with '$'. */
			if (indexDefKeyKey[0] == '$')
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Index key contains an illegal field name: field name starts with '$'.")));
			}

			/* Case 2: Index key field should not start with '.'. */
			if (indexDefKeyKey[0] == '.')
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("Index keys cannot contain an empty field.")));
			}

			/* Case 3: Index keyPath should not have double dots in the path */
			if (strstr(indexDefKeyKey, DOUBLE_DOT_IN_INDEX_PATH) != NULL)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("Index keys cannot contain an empty field.")));
			}

			/* Index path key cannot be '_fts' (full text search )*/
			if (strcmp(indexDefKeyKey, "_fts") == 0)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Index key contains an illegal field name: '_fts'.")));
			}

			/* wildcard index on a path ? */

			const char *dotWildCardSuffix = strstr(indexDefKeyKey,
												   DOT_WILDCARD_INDEX_SUFFIX);
			isWildcardKeyPath = dotWildCardSuffix != NULL;
			if (isWildcardKeyPath)
			{
				/* Case 4: Index path should not have anything after wildcard */
				if (dotWildCardSuffix + strlen(DOT_WILDCARD_INDEX_SUFFIX) <
					indexDefKeyKey + strlen(indexDefKeyKey))
				{
					ereport(ERROR, (errcode(MongoCannotCreateIndex),
									errmsg("Index key contains an illegal field name")));
				}
			}
			StringView indexPath = CreateStringViewFromString(indexDefKeyKey);

			/* Case 5: IndexPath should never end with '.'. */
			if (StringViewEndsWith(&indexPath, '.'))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("Index keys cannot contain an empty field.")));
			}

			/* Case 6: Iterate through the dotted path and make sure the field should not start with '$' */
			StringView indexPathSubstring = StringViewFindSuffix(&indexPath, '.');

			while (indexPathSubstring.string != NULL)
			{
				if (StringViewStartsWith(&indexPathSubstring, '$') &&
					(strcmp(indexPathSubstring.string, WILDCARD_INDEX_SUFFIX) != 0) &&
					(strcmp(indexPathSubstring.string, "$id") != 0))
				{
					ereport(ERROR, (errcode(MongoCannotCreateIndex),
									errmsg(
										"Index key contains an illegal field name: field name starts with '$'.")));
				}
				indexPath = StringViewSubstring(&indexPath,
												indexPathSubstring.length + 1);
				indexPathSubstring = StringViewFindSuffix(&indexPath, '.');
			}
		}

		/* determine keypath */
		char *keyPath = NULL;
		MongoIndexKind indexKind = MongoIndexKind_Regular;
		if (isWildcardKeyPath)
		{
			if (wildcardOnWholeDocument)
			{
				/*
				 * No key paths, leave it to be NULL. This is ok since we
				 * might not have a prefixing paths for a wildcard index.
				 */
			}
			else
			{
				/* delete DOT_WILDCARD_INDEX_SUFFIX */
				int trimLen = strlen(indexDefKeyKey) - strlen(DOT_WILDCARD_INDEX_SUFFIX);
				keyPath = pnstrdup(indexDefKeyKey, trimLen);
			}
		}
		else
		{
			keyPath = pstrdup(indexDefKeyKey);
		}

		/* treat empty string as a NULL pointer */
		keyPath = (keyPath && strcmp(keyPath, "") == 0) ? NULL : keyPath;

		/*
		 * Disallow empty path if it's not a wildcard index on whole document.
		 */
		if (!wildcardOnWholeDocument && keyPath == NULL)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg("Index keys cannot be an empty field")));
		}

		/*
		 * TODO: Also need to parse value of indexDefKeyIter for the index
		 *       ordering direction, i.e.: (1 or -1), but our bson GIN/RUM operators
		 *       anyway don't know how to handle the ordering yet.
		 *
		 * Validating the value of indexDefKeyIter which could also be a text that determines
		 * the index type, such as "text", "2d", "2dsphere", otherwise it should be a number
		 */
		const bson_value_t *keyValue = bson_iter_value(&indexDefKeyIter);
		if (BSON_ITER_HOLDS_UTF8(&indexDefKeyIter))
		{
			/* Spec value can be an empty value or invalid mongo index type */
			if (keyValue->value.v_utf8.len == 0)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Values in the index key pattern cannot be empty strings")));
			}
			else
			{
				bool isValidMongoIndexAndSupported = false;
				for (int i = 0; i < NumberOfMongoIndexTypes; i++)
				{
					MongoIndexSupport idxSupport = MongoIndexSupportedList[i];
					if (strcmp(keyValue->value.v_utf8.str, idxSupport.mongoIndexName) ==
						0)
					{
						if (idxSupport.isSupported)
						{
							indexKind = idxSupport.indexKind;
							isValidMongoIndexAndSupported = true;
							break;
						}
						else
						{
							ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
											errmsg("%s mongo index is not supported yet",
												   keyValue->value.v_utf8.str),
											errhint("%s mongo index is not supported yet",
													keyValue->value.v_utf8.str)));
						}
					}
				}


				if (!isValidMongoIndexAndSupported)
				{
					ereport(ERROR, (errcode(MongoCannotCreateIndex),
									errmsg("Unknown index plugin %s",
										   BsonValueToJsonForLogging(keyValue))));
				}
			}
		}
		else if (BSON_ITER_HOLDS_NUMBER(&indexDefKeyIter))
		{
			double doubleValue = BsonValueAsDouble(keyValue);

			/* Index Key Spec value can't be zero */
			if (doubleValue == (double) 0)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Values in the index key pattern cannot be 0.")));
			}

			if (isWildcardKeyPath && (doubleValue < 0))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"A numeric value in a $** index key pattern must be positive.")));
			}
		}
		else
		{
			/* All other data types can't be specified as key spec value */
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"Values in v:2 index key pattern cannot be of type %s. Only numbers > 0, numbers < 0, and strings are allowed.",
								BsonTypeName(keyValue->value_type))));
		}

		/* If we have a wildcard path that's compounded, fail if it's not a text index */
		if (isWildcardKeyPath && list_length(indexDefKey->keyPathList) != 0)
		{
			/* Root wildcard & Text is a valid combo for compound index */
			if (!wildcardOnWholeDocument || indexKind != MongoIndexKind_Text)
			{
				/*
				 * Already parsed a keyPath before, so it's a compound index.
				 * But compound indexes cannot contain a wildcard key.
				 */
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("wildcard indexes do not allow compounding")));
			}
		}

		if (indexKind == MongoIndexKind_Hashed)
		{
			if (isWildcardKeyPath)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Index key contains an illegal field name: field name starts with '$'.")));
			}

			numHashedIndexes++;
		}

		if (indexKind == MongoIndexingKind_CosmosSearch)
		{
			indexDefKey->hasCosmosIndexes = true;
		}

		if (indexKind == MongoIndexKind_2d)
		{
			ReportFeatureUsage(FEATURE_CREATE_INDEX_2D);
			EnsureGeospatialFeatureEnabled();
			if (indexDefKey->has2dIndex)
			{
				/* Can't have more than one 2d index fields */
				ereport(ERROR, (errcode(MongoLocation16800),
								errmsg("can't have 2 geo fields")));
			}
			else if (indexDefKey->keyPathList != NIL)
			{
				ereport(ERROR, (errcode(MongoLocation16801),
								errmsg("2d has to be first in index")));
			}
			else if (isWildcardKeyPath)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Index key contains an illegal field name: field name starts with '$'.")));
			}

			indexDefKey->has2dIndex = true;
		}

		if (indexKind == MongoIndexKind_2dsphere)
		{
			ReportFeatureUsage(FEATURE_CREATE_INDEX_2DSPHERE);
			EnsureGeospatialFeatureEnabled();
			if (isWildcardKeyPath)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Index key contains an illegal field name: field name starts with '$'.")));
			}

			indexDefKey->has2dsphereIndex = true;
		}

		bool addIndexKey = !wildcardOnWholeDocument;
		if (indexKind == MongoIndexKind_Text)
		{
			ReportFeatureUsage(FEATURE_CREATE_INDEX_TEXT);
			if ((allindexKinds & MongoIndexKind_Text) == MongoIndexKind_Text &&
				(lastIndexKind != MongoIndexKind_Text))
			{
				/* If a prior column already had textIndexes, it must be adjacent */
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"'text' fields in index must all be adjacent")));
			}

			/* 'text' indexes don't support wildcards on subpaths - only the root */
			if (isWildcardKeyPath && !wildcardOnWholeDocument)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"Index key contains an illegal field name: field name starts with '$'.")));
			}

			if (!wildcardOnWholeDocument)
			{
				TextIndexWeights *textWeight = palloc0(sizeof(TextIndexWeights));
				textWeight->path = keyPath;
				textWeight->weight = 1.0;

				indexDefKey->textPathList = lappend(indexDefKey->textPathList,
													textWeight);
			}

			/* We only add the first key - the rest are tracked within the index itself. */
			if (lastIndexKind == MongoIndexKind_Text)
			{
				addIndexKey = false;
			}
			else
			{
				/* We always add the first text key (even if it's the root wildcard) */
				addIndexKey = true;
			}
		}

		allindexKinds |= indexKind;
		lastIndexKind = indexKind;
		if (isWildcardKeyPath)
		{
			wildcardIndexKind |= indexKind;
		}

		if (addIndexKey)
		{
			IndexDefKeyPath *indexDefKeyPath = palloc0(sizeof(IndexDefKeyPath));
			indexDefKeyPath->indexKind = indexKind;
			indexDefKeyPath->path = keyPath;
			indexDefKeyPath->isWildcard = isWildcardKeyPath;

			indexDefKey->keyPathList = lappend(indexDefKey->keyPathList, indexDefKeyPath);
		}

		indexDefKey->isWildcard = isWildcardKeyPath;
	}

	/* Check the number of types of indexes excluding the "Regular" index kind */
	if (pg_popcount32(allindexKinds & ~MongoIndexKind_Regular) > 1)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"Can't use more than one index plugin for a single index.")));
	}

	if ((allindexKinds & MongoIndexKind_2dsphere) == MongoIndexKind_2dsphere &&
		(allindexKinds & MongoIndexKind_Regular) == MongoIndexKind_Regular)
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg(
							"Compound Regular & 2dsphere indexes are not supported yet")));
	}

	if (numHashedIndexes > 1)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"A maximum of one index field is allowed to be hashed but found %d",
							numHashedIndexes)));
	}

	if (indexDefKey->hasCosmosIndexes &&
		list_length(indexDefKey->keyPathList) != 1)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"A maximum of one index field is allowed for cdb indexes")));
	}

	indexDefKey->hasHashedIndexes = numHashedIndexes > 0;
	indexDefKey->hasTextIndexes = (allindexKinds & MongoIndexKind_Text) != 0;
	return indexDefKey;
}


/*
 * Parse cosmosSearchOptions from the given indexDefDocIter.
 * Index of ivfflat and hnsw is supported.
 * ivfflat:
 *    { "kind": "vector-ivf", "numLists": 100, "similarity": "COS", "dimensions": 3 }
 * hnsw:
 *    { "kind": "vector-hnsw", "m": 16, "efConstruction": 64, "similarity": "COS", "dimensions": 3 }
 */
static CosmosSearchOptions *
ParseCosmosSearchOptionsDoc(const bson_iter_t *indexDefDocIter)
{
	ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR);
	CosmosSearchOptions *cosmosSearchOptions = palloc0(sizeof(CosmosSearchOptions));

	bson_iter_t cosmosSearchIter;
	bson_iter_recurse(indexDefDocIter, &cosmosSearchIter);
	while (bson_iter_next(&cosmosSearchIter))
	{
		const char *searchOptionsIterKey = bson_iter_key(&cosmosSearchIter);

		const bson_value_t *keyValue = bson_iter_value(&cosmosSearchIter);
		if (strcmp(searchOptionsIterKey, "kind") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(&cosmosSearchIter))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("search index kind must be a string not %s",
									   BsonTypeName(bson_iter_type(&cosmosSearchIter)))));
			}

			StringView str = {
				.string = keyValue->value.v_utf8.str, .length = keyValue->value.v_utf8.len
			};
			if (StringViewEqualsCString(&str, "vector-ivf"))
			{
				ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR_TYPE_IVFFLAT);
				cosmosSearchOptions->indexKind = MongoCdbIndexKind_VectorSearch_Ivf;
			}
			else if (StringViewEqualsCString(&str, "vector-hnsw"))
			{
				if (!EnableVectorHNSWIndex)
				{
					/* Safe guard against the helio_api.enableVectorHNSWIndex GUC */
					ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
									errmsg(
										"hnsw index is not supported for this cluster tier")));
				}
				ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR_TYPE_HNSW);
				cosmosSearchOptions->indexKind = MongoCdbIndexKind_VectorSearch_Hnsw;
			}
			else
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("Invalid search index kind %s",
									   str.string)));
			}
		}
		else if (strcmp(searchOptionsIterKey, VECTOR_PARAMETER_NAME_IVF_NLISTS) == 0)
		{
			if (!BsonValueIsNumber(keyValue))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("%s must be a number not %s",
									   VECTOR_PARAMETER_NAME_IVF_NLISTS,
									   BsonTypeName(bson_iter_type(&cosmosSearchIter)))));
			}

			cosmosSearchOptions->vectorOptions.numLists = BsonValueAsInt32(keyValue);

			if (cosmosSearchOptions->vectorOptions.numLists < IVFFLAT_MIN_LISTS)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"%s must be greater than or equal to %d not %d",
									VECTOR_PARAMETER_NAME_IVF_NLISTS,
									IVFFLAT_MIN_LISTS,
									cosmosSearchOptions->vectorOptions.numLists)));
			}

			if (cosmosSearchOptions->vectorOptions.numLists > IVFFLAT_MAX_LISTS)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"%s must be less or equal than or equal to %d not %d",
									VECTOR_PARAMETER_NAME_IVF_NLISTS,
									IVFFLAT_MAX_LISTS,
									cosmosSearchOptions->vectorOptions.numLists)));
			}
		}
		else if (strcmp(searchOptionsIterKey, VECTOR_PARAMETER_NAME_HNSW_M) == 0)
		{
			if (!BsonValueIsNumber(keyValue))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("%s must be a number not %s",
									   VECTOR_PARAMETER_NAME_HNSW_M,
									   BsonTypeName(bson_iter_type(&cosmosSearchIter)))));
			}

			cosmosSearchOptions->vectorOptions.m = BsonValueAsInt32(keyValue);

			if (cosmosSearchOptions->vectorOptions.m < HNSW_MIN_M)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("%s must be greater than or equal to %d not %d",
									   VECTOR_PARAMETER_NAME_HNSW_M,
									   HNSW_MIN_M,
									   cosmosSearchOptions->vectorOptions.m)));
			}

			if (cosmosSearchOptions->vectorOptions.m > HNSW_MAX_M)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("%s must be less than or equal to %d not %d",
									   VECTOR_PARAMETER_NAME_HNSW_M,
									   HNSW_MAX_M,
									   cosmosSearchOptions->vectorOptions.m)));
			}
		}
		else if (strcmp(searchOptionsIterKey,
						VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION) == 0)
		{
			if (!BsonValueIsNumber(keyValue))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("%s must be a number not %s",
									   VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION,
									   BsonTypeName(bson_iter_type(&cosmosSearchIter)))));
			}

			cosmosSearchOptions->vectorOptions.efConstruction = BsonValueAsInt32(
				keyValue);

			if (cosmosSearchOptions->vectorOptions.efConstruction <
				HNSW_MIN_EF_CONSTRUCTION)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"%s must be greater than or equal to %d not %d",
									VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION,
									HNSW_MIN_EF_CONSTRUCTION,
									cosmosSearchOptions->vectorOptions.efConstruction)));
			}

			if (cosmosSearchOptions->vectorOptions.efConstruction >
				HNSW_MAX_EF_CONSTRUCTION)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"%s must be less than or equal to %d not %d",
									VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION,
									HNSW_MAX_EF_CONSTRUCTION,
									cosmosSearchOptions->vectorOptions.efConstruction)));
			}
		}
		else if (strcmp(searchOptionsIterKey, "similarity") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(&cosmosSearchIter))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"search index distance metric must be a string not %s",
									BsonTypeName(bson_iter_type(&cosmosSearchIter)))));
			}

			StringView str = {
				.string = keyValue->value.v_utf8.str, .length = keyValue->value.v_utf8.len
			};
			if (StringViewEqualsCString(&str, "L2"))
			{
				ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR_L2);
				cosmosSearchOptions->vectorOptions.distanceMetric =
					VectorIndexDistanceMetric_L2Distance;
			}
			else if (StringViewEqualsCString(&str, "IP"))
			{
				ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR_IP);
				cosmosSearchOptions->vectorOptions.distanceMetric =
					VectorIndexDistanceMetric_IPDistance;
			}
			else if (StringViewEqualsCString(&str, "COS"))
			{
				ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR_COS);
				cosmosSearchOptions->vectorOptions.distanceMetric =
					VectorIndexDistanceMetric_CosineDistance;
			}
			else
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("Invalid search index distance kind %s",
									   str.string)));
			}
		}
		else if (strcmp(searchOptionsIterKey, "dimensions") == 0)
		{
			if (!BsonValueIsNumber(keyValue))
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("dimensions must be a number not %s",
									   BsonTypeName(bson_iter_type(&cosmosSearchIter)))));
			}

			cosmosSearchOptions->vectorOptions.numDimensions = BsonValueAsInt32(keyValue);
		}
		else
		{
			ereport(ERROR, (errcode(MongoUnknownBsonField),
							errmsg("BSON field 'createIndexes.cosmosSearch.%s' is an "
								   "unknown field", searchOptionsIterKey)));
		}
	}

	if (cosmosSearchOptions->indexKind == MongoCdbIndexKind_Unknown)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg("cosmosSearch index kind must be specified")));
	}

	/* Check search option for ivf */
	if (cosmosSearchOptions->vectorOptions.numLists > 0 &&
		cosmosSearchOptions->indexKind != MongoCdbIndexKind_VectorSearch_Ivf)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"%s must be specified only for vector-ivf indexes",
							VECTOR_PARAMETER_NAME_IVF_NLISTS)));
	}

	/* Set default numLists for ivfflat */
	if (cosmosSearchOptions->vectorOptions.numLists == 0 &&
		cosmosSearchOptions->indexKind == MongoCdbIndexKind_VectorSearch_Ivf)
	{
		cosmosSearchOptions->vectorOptions.numLists = IVFFLAT_DEFAULT_LISTS;
	}

	/* Check search option for hnsw */
	if (cosmosSearchOptions->vectorOptions.m > 0 &&
		cosmosSearchOptions->indexKind != MongoCdbIndexKind_VectorSearch_Hnsw)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"%s must be specified only for vector-hnsw indexes",
							VECTOR_PARAMETER_NAME_HNSW_M)));
	}

	/* Set default m for hnsw */
	if (cosmosSearchOptions->vectorOptions.m == 0 &&
		cosmosSearchOptions->indexKind == MongoCdbIndexKind_VectorSearch_Hnsw)
	{
		cosmosSearchOptions->vectorOptions.m = HNSW_DEFAULT_M;
	}

	if (cosmosSearchOptions->vectorOptions.efConstruction > 0 &&
		cosmosSearchOptions->indexKind != MongoCdbIndexKind_VectorSearch_Hnsw)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"%s must be specified only for vector-hnsw indexes",
							VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION)));
	}

	/* Set default efConstruction for hnsw */
	if (cosmosSearchOptions->vectorOptions.efConstruction == 0 &&
		cosmosSearchOptions->indexKind == MongoCdbIndexKind_VectorSearch_Hnsw)
	{
		cosmosSearchOptions->vectorOptions.efConstruction = HNSW_DEFAULT_EF_CONSTRUCTION;
	}

	/* Check efConstruction is greater than or equal to m * 2 */
	if ((cosmosSearchOptions->vectorOptions.efConstruction <
		 cosmosSearchOptions->vectorOptions.m * 2) &&
		cosmosSearchOptions->indexKind == MongoCdbIndexKind_VectorSearch_Hnsw)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"%s must be greater than or equal to 2 * m for vector-hnsw indexes",
							VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION)));
	}

	if (cosmosSearchOptions->vectorOptions.distanceMetric !=
		VectorIndexDistanceMetric_Unknown &&
		cosmosSearchOptions->indexKind != MongoCdbIndexKind_VectorSearch_Ivf &&
		cosmosSearchOptions->indexKind != MongoCdbIndexKind_VectorSearch_Hnsw)
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"similarity metric must be specified only for vector indexes")));
	}

	if ((cosmosSearchOptions->indexKind == MongoCdbIndexKind_VectorSearch_Ivf ||
		 cosmosSearchOptions->indexKind == MongoCdbIndexKind_VectorSearch_Hnsw) &&
		(cosmosSearchOptions->vectorOptions.numDimensions <= 1 ||
		 cosmosSearchOptions->vectorOptions.distanceMetric ==
		 VectorIndexDistanceMetric_Unknown))
	{
		ereport(ERROR, (errcode(MongoCannotCreateIndex),
						errmsg(
							"vector index must specify dimensions greater than 1 and similarity metric")));
	}

	return cosmosSearchOptions;
}


/*
 * ParseIndexDefWildcardProjDoc returns a BsonIntermediatePathNode object by
 * parsing value of pgbson iterator that points to the "wildcardProjection"
 * field of an index definiton document.
 *
 * Also extends error messages related with Mongo errors that might be thrown
 * when parsing "wildcardProjection" field of an index specification document.
 */
static BsonIntermediatePathNode *
ParseIndexDefWildcardProjDoc(const bson_iter_t *indexDefDocIter)
{
	bson_iter_t indexDefWPIter;
	bson_iter_recurse(indexDefDocIter, &indexDefWPIter);

	BsonIntermediatePathNode *wildcardProjectionTree = NULL;

	MemoryContext savedMemoryContext = CurrentMemoryContext;
	PG_TRY();
	{
		wildcardProjectionTree = BuildBsonPathTreeForWPDocument(&indexDefWPIter);
		CheckWildcardProjectionTree(wildcardProjectionTree);
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(savedMemoryContext);
		RethrowPrependMongoError("Failed to parse: wildcardProjection :: "
								 "caused by :: ");
	}
	PG_END_TRY();

	return wildcardProjectionTree;
}


/*
 * WildcardProjDocsAreEquivalent returns true if given two wildcardProjection
 * documents are equivalent.
 */
bool
WildcardProjDocsAreEquivalent(const pgbson *leftWPDocument, const pgbson *rightWPDocument)
{
	bson_iter_t leftWPDocIter;
	PgbsonInitIterator(leftWPDocument, &leftWPDocIter);

	BsonIntermediatePathNode *leftWPDocTree = BuildBsonPathTreeForWPDocument(
		&leftWPDocIter);

	WildcardProjectionPathOps *leftWPOps = ResolveWPPathOpsFromTree(leftWPDocTree);

	bson_iter_t rightWPDocIter;
	PgbsonInitIterator(rightWPDocument, &rightWPDocIter);

	BsonIntermediatePathNode *rightWPDocTree = BuildBsonPathTreeForWPDocument(
		&rightWPDocIter);

	WildcardProjectionPathOps *rightWPOps = ResolveWPPathOpsFromTree(rightWPDocTree);

	if (leftWPOps->idFieldInclusion != rightWPOps->idFieldInclusion ||
		leftWPOps->nonIdFieldInclusion != rightWPOps->nonIdFieldInclusion)
	{
		return false;
	}

	bool numNonIdFieldPathsEqual = list_length(leftWPOps->nonIdFieldPathList) ==
								   list_length(rightWPOps->nonIdFieldPathList);
	if (!numNonIdFieldPathsEqual)
	{
		return false;
	}

	SortStringList(leftWPOps->nonIdFieldPathList);
	SortStringList(rightWPOps->nonIdFieldPathList);
	return StringListsAreEqual(leftWPOps->nonIdFieldPathList,
							   rightWPOps->nonIdFieldPathList);
}


/*
 * BuildBsonPathTreeForWPDocument is a convenient wrapper around BuildBsonPathTree
 * to populate given tree based on given wildcardProjection document iterator.
 */
static BsonIntermediatePathNode *
BuildBsonPathTreeForWPDocument(bson_iter_t *indexDefWPIter)
{
	/*
	 * Here we only care about the tree itself and callers are expected to
	 * verify if there are any conflicting inclusion specifications in the
	 * document, if they are required to do so.
	 */
	bool hasFieldsIgnore = false;
	bool forceLeafExpression = false;
	BuildBsonPathTreeContext context = { 0 };
	BuildBsonPathTreeFunctions pathTreeFuncs = DefaultPathTreeFuncs;
	pathTreeFuncs.createLeafNodeFunc = CreateIndexesCreateLeafNode;
	context.buildPathTreeFuncs = &pathTreeFuncs;
	context.skipParseAggregationExpressions = true;
	return BuildBsonPathTree(indexDefWPIter, &context, forceLeafExpression,
							 &hasFieldsIgnore);
}


/*
 * CheckWildcardProjectionTree throws an error if wildcardProjection
 * specification that corresponds to given tree is not valid.
 */
static void
CheckWildcardProjectionTree(const BsonIntermediatePathNode *wildcardProjectionTree)
{
	/*
	 * We will pass the first child node to skip validating the dummy root
	 * node. If the root doesn't have any children, then the input
	 * "wildcardProjection" is empty and so the tree is.
	 *
	 * In that case, we expect ParseIndexDefKeyDocument to throw an error
	 * instead of this function since we don't want to wrap the error message
	 * via ParseIndexDefWildcardProjDoc in that case. This is to throw the
	 * same error message that Mongo throws for empty "wildcardProjection"
	 * specification.
	 */
	if (!IntermediateNodeHasChildren(wildcardProjectionTree))
	{
		return;
	}

	/*
	 * Initially we don't enforce any inclusion specifications.
	 *
	 * CheckWildcardProjectionTreeInternal will determine that at the time
	 * that it processes the first leaf node and will test other leaf nodes to
	 * see if there is a conflict.
	 */
	bool isTopLevel = true;
	WildcardProjFieldInclusionMode expectedWPInclusionMode = WP_IM_INVALID;
	CheckWildcardProjectionTreeInternal(
		wildcardProjectionTree,
		isTopLevel,
		expectedWPInclusionMode);
}


/*
 * CheckWildcardProjectionTreeInternal is the internal function to
 * CheckWildcardProjectionTree that recursively validates all nodes
 * in given tree node.
 *
 * isTopLevel should be set to false in recursive calls and should
 * only be set to true by CheckWildcardProjectionTree.
 *
 * expectedWPInclusionMode is the inclusion mode that needs to be enforced
 * when traversing the tree. For this reason, it should be set to
 * WP_IM_INVALID for the top-level call.
 */
static WildcardProjFieldInclusionMode
CheckWildcardProjectionTreeInternal(const BsonIntermediatePathNode *treeParentNode,
									bool isTopLevel,
									WildcardProjFieldInclusionMode
									expectedWPInclusionMode)
{
	check_stack_depth();

	Assert(!isTopLevel || expectedWPInclusionMode == WP_IM_INVALID);

	const BsonPathNode *treeNode;
	foreach_child(treeNode, treeParentNode)
	{
		CHECK_FOR_INTERRUPTS();

		switch (treeNode->nodeType)
		{
			case NodeType_Intermediate:
			{
				const BsonIntermediatePathNode *intermediateNode = CastAsIntermediateNode(
					treeNode);
				if (!IntermediateNodeHasChildren(intermediateNode))
				{
					ereport(ERROR, (errcode(MongoFailedToParse),
									errmsg("An empty sub-projection is not a valid "
										   "value. Found empty object at path")));
				}

				/*
				 * If expectedWPInclusionMode is not WP_IM_INVALID, then
				 * CheckWildcardProjectionTreeInternal will make sure that
				 * other leaf nodes have the same inclusion specification
				 * too.
				 *
				 * Otherwise, it would determine expectedWPInclusionMode
				 * and return the determined value. For this reason, it is
				 * ok to update expectedWPInclusionMode here in all cases.
				 */
				bool recurseIsTopLevel = false;
				expectedWPInclusionMode =
					CheckWildcardProjectionTreeInternal(
						intermediateNode,
						recurseIsTopLevel,
						expectedWPInclusionMode);

				break;
			}

			case NodeType_LeafField:
			{
				/*
				 * kBanComputedFields might or might not be a config that
				 * Mongo allows users to specify, not sure .. But for now,
				 * here we use exactly the same error message that Mongo
				 * does.
				 */
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Bad projection specification, cannot use computed fields when parsing a spec in kBanComputedFields mode")));
			}

			case NodeType_LeafIncluded:
			case NodeType_LeafExcluded:
			{
				WildcardProjFieldInclusionMode nodeInclusionMode =
					(treeNode->nodeType == NodeType_LeafIncluded) ? WP_IM_INCLUDE :
					WP_IM_EXCLUDE;

				if (isTopLevel &&
					StringViewEquals(&treeNode->field, &IdFieldStringView))
				{
					/* do nothing */
				}
				else if (expectedWPInclusionMode == WP_IM_INVALID)
				{
					expectedWPInclusionMode = nodeInclusionMode;
				}
				else if (expectedWPInclusionMode != nodeInclusionMode)
				{
					CreateIndexesLeafPathNodeData *leafPathNode =
						(CreateIndexesLeafPathNodeData *) treeNode;
					ereport(ERROR, (errcode(MongoFailedToParse),
									errmsg("Cannot do %s on field %s in %s projection",
										   WPFieldInclusionModeString(nodeInclusionMode),
										   leafPathNode->relativePath,
										   WPFieldInclusionModeString(
											   expectedWPInclusionMode))));
				}

				break;
			}

			default:
			{
				ereport(ERROR, (errmsg("got unexpected tree node type when "
									   "traversing the (internal) tree "
									   "representation of 'wildcardProjection' "
									   "specification")));
			}
		}
	}

	return expectedWPInclusionMode;
}


/*
 * WPFieldInclusionModeString returns string representation of given
 * WildcardProjFieldInclusionMode value.
 */
static const char *
WPFieldInclusionModeString(WildcardProjFieldInclusionMode wpInclusionMode)
{
	switch (wpInclusionMode)
	{
		case WP_IM_INCLUDE:
		{
			return "inclusion";
		}

		case WP_IM_EXCLUDE:
		{
			return "exclusion";
		}

		default:
		{
			ereport(ERROR, (errmsg("got unknown wildcard projection "
								   "inclusion mode")));
		}
	}
}


/*
 * GenerateWildcardProjDocument returns a pgbson object based on given tree that
 * identifies the "wildcardProjection" specification of an index.
 */
static pgbson *
GenerateWildcardProjDocument(const BsonIntermediatePathNode *wildcardProjectionTree)
{
	/* pass first child of wildcardProjectionTree to skip dummy root node */
	bool isTopLevel = true;
	return GenerateWildcardProjDocumentInternal(
		wildcardProjectionTree,
		isTopLevel);
}


/*
 * GenerateWildcardProjDocumentInternal is the internal function to
 * GenerateWildcardProjDocument to be used when recursively traversing nodes
 * of given tree.
 *
 * isTopLevel should be set to false in recursive calls and should only be
 * set to true by GenerateWildcardProjDocument.
 */
static pgbson *
GenerateWildcardProjDocumentInternal(const BsonIntermediatePathNode *treeParentNode,
									 bool isTopLevel)
{
	check_stack_depth();

	pgbson_writer objectWriter;
	PgbsonWriterInit(&objectWriter);

	bool gotTopLevelIdFieldInclusionSpec = false;

	const BsonPathNode *treeNode;
	foreach_child(treeNode, treeParentNode)
	{
		CHECK_FOR_INTERRUPTS();

		switch (treeNode->nodeType)
		{
			case NodeType_Intermediate:
			{
				const BsonIntermediatePathNode *intermediateNode = CastAsIntermediateNode(
					treeNode);
				if (intermediateNode->hasExpressionFieldsInChildren)
				{
					ereport(ERROR, (errmsg("unexpectedly got an Intermediate tree "
										   "node with field when traversing the "
										   "(internal) tree representation of "
										   "'wildcardProjection' specification")));
				}

				if (!IntermediateNodeHasChildren(intermediateNode))
				{
					ereport(ERROR, (errmsg("unexpectedly got an Intermediate "
										   "tree node that has no children when "
										   "traversing the (internal) tree "
										   "representation of 'wildcardProjection' "
										   "specification")));
				}

				bool recurseIsTopLevel = false;
				PgbsonWriterAppendDocument(&objectWriter,
										   treeNode->field.
										   string,
										   treeNode->field.
										   length,
										   GenerateWildcardProjDocumentInternal(
											   intermediateNode,
											   recurseIsTopLevel));

				break;
			}

			case NodeType_LeafIncluded:
			case NodeType_LeafExcluded:
			{
				if (isTopLevel &&
					StringViewEquals(&treeNode->field,
									 &IdFieldStringView))
				{
					gotTopLevelIdFieldInclusionSpec = true;
				}

				PgbsonWriterAppendBool(&objectWriter,
									   treeNode->field.string,
									   treeNode->field.length,
									   treeNode->nodeType == NodeType_LeafIncluded ? 1 :
									   0);

				break;
			}

			default:
			{
				ereport(ERROR, (errmsg("got unexpected tree node type when "
									   "traversing the (internal) tree "
									   "representation of 'wildcardProjection' "
									   "specification")));
			}
		}
	}

	if (isTopLevel && !gotTopLevelIdFieldInclusionSpec)
	{
		/* exclude "_id" field by default */
		PgbsonWriterAppendBool(&objectWriter, ID_FIELD_KEY, strlen(ID_FIELD_KEY), 0);
	}

	return PgbsonWriterGetPgbson(&objectWriter);
}


/*
 * ParseIndexDefPartFilterDocument returns an Expr node parsing value of
 * pgbson iterator that points to the "partialFilterExpression" field of
 * an index definiton document.
 *
 * Returns a Const(true) node if given "partialFilterExpression" points
 * to an empty document.
 */
static Expr *
ParseIndexDefPartFilterDocument(const bson_iter_t *indexDefDocIter)
{
	bson_iter_t partFilterExprIter;
	bson_iter_recurse(indexDefDocIter, &partFilterExprIter);

	BsonQueryOperatorContext context = { 0 };
	context.documentExpr = (Expr *) MakeSimpleDocumentVar();
	context.inputType = MongoQueryOperatorInputType_Bson;
	context.simplifyOperators = false;
	context.coerceOperatorExprIfApplicable = true;
	List *partialFilterQuals = CreateQualsFromQueryDocIterator(&partFilterExprIter,
															   &context);

	if (TargetListContainsGeonearOp(context.targetEntries))
	{
		ThrowGeoNearNotAllowedInContextError();
	}

	Expr *partialFilterExpr = make_ands_explicit(partialFilterQuals);

	if (!EnableExtendedIndexFilters)
	{
		bool isTopLevel = true;
		CheckPartFilterExprOperatorsWalker((Node *) partialFilterExpr,
										   (void *) isTopLevel);
	}

	return partialFilterExpr;
}


/*
 * CheckPartFilterExprOperatorsWalker throws an error if given qual node
 * contains an operator that is not supported for "partialFilterExpression"
 * field of an index definiton document.
 *
 * "context" would be casted to a bool to determine whether given node refers
 * to a top-level expression of "partialFilterExpression" document and must
 * be set to false when recursing down to child expressions.
 */
static bool
CheckPartFilterExprOperatorsWalker(Node *node, void *context)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		/* ok, do not recurse anymore */
		return false;
	}

	if (IsA(node, BoolExpr))
	{
		BoolExpr *boolExpr = (BoolExpr *) node;
		if (boolExpr->boolop == AND_EXPR)
		{
			bool isTopLevel = (bool) context;
			if (!isTopLevel)
			{
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg("$and only supported in partialFilterExpression "
									   "at top level")));
			}
		}
		else if (boolExpr->boolop == OR_EXPR)
		{
			ThrowUnsupportedPartFilterExprError(node);
		}
		else if (boolExpr->boolop == NOT_EXPR)
		{
			ThrowUnsupportedPartFilterExprError(node);
		}
		else
		{
			ereport(ERROR, (errmsg("unknown bool operator")));
		}
	}
	else if (IsA(node, OpExpr) || IsA(node, FuncExpr))
	{
		const MongoQueryOperator *operator = NULL;
		List *args = NIL;
		operator = GetMongoQueryOperatorFromExpr(node, &args);
		bool isFuncExpr = IsA(node, FuncExpr);

		switch (operator->operatorType)
		{
			case QUERY_OPERATOR_EQ:
			case QUERY_OPERATOR_GT:
			case QUERY_OPERATOR_GTE:
			case QUERY_OPERATOR_LT:
			case QUERY_OPERATOR_LTE:
			{
				/* These are supported operators for partial filter expressions */
				if (isFuncExpr)
				{
					/* Fail - this is unexpected for create_indexes */
					/* as we don't expect them to appear as function expressions */
					ereport(ERROR, (errmsg(
										"Unexpected - found Function expression for operator"
										" %s partial filter expression",
										operator->mongoOperatorName)));
				}
				break;
			}

			case QUERY_OPERATOR_TYPE:
			{
				/* $type can show up as a FuncExpr and it's okay - only exact matches allowed
				 * so the PFE engine will just allow pushdown on $type being exact */
				break;
			}

			case QUERY_OPERATOR_EXISTS:
			{
				/* check unexpected cases to be on the safe side */
				if (list_length(args) != 2)
				{
					ereport(ERROR, (errmsg("got unexpected number of args for "
										   "$exists operator")));
				}

				Node *existsRhsArg = lsecond(args);
				if (!existsRhsArg || !IsA(existsRhsArg, Const) ||
					!((Const *) existsRhsArg)->constvalue)
				{
					ereport(ERROR, (errmsg("got a non-Const node or a null "
										   "const value for second argument of "
										   "$exists operator")));
				}

				Datum existsRhsConstValue = ((Const *) existsRhsArg)->constvalue;
				pgbson *existsRhsBson = (pgbson *) DatumGetPointer(existsRhsConstValue);
				pgbsonelement element;
				PgbsonToSinglePgbsonElement(existsRhsBson, &element);
				bool existsPositiveMatch = BsonValueAsInt64(&element.bsonValue) != 0;
				if (!existsPositiveMatch)
				{
					/* $exists is supported only when rhs evaluates to true */
					ThrowUnsupportedPartFilterExprError(node);
				}

				/* $exists can show up as a FuncExpr and it's okay - only exact matches allowed
				 * so the PFE engine will just allow pushdown on $exists being exact */
				break;
			}

			case QUERY_OPERATOR_UNKNOWN:
			{
				ereport(ERROR, (errcode(MongoBadValue), errmsg(
									"unknown mongo operator")));
			}

			default:
			{
				ThrowUnsupportedPartFilterExprError(node);
			}
		}
	}

	bool isTopLevel = false;
	return expression_tree_walker(node, CheckPartFilterExprOperatorsWalker,
								  (void *) isTopLevel);
}


/*
 * ThrowUnsupportedPartFilterExprError throws a MongoCannotCreateIndex error
 * for given unsupported partial index expression node.
 */
static void
ThrowUnsupportedPartFilterExprError(Node *node)
{
	ereport(ERROR, (errcode(MongoCannotCreateIndex),
					errmsg("unsupported expression in partial index: %s",
						   GetPartFilterExprNodeRepr(node))));
}


/*
 * GetPartFilterExprNodeRepr returns a string representation for given node
 * that can be used in the error message that ThrowUnsupportedPartFilterExprError
 * throws.
 */
static char *
GetPartFilterExprNodeRepr(Node *node)
{
	PartFilterExprNodeReprWalkerContext context = {
		.indentationLevel = 0,
		.reprStr = makeStringInfo()
	};
	GetPartFilterExprNodeReprWalker(node, (void *) &context);
	return context.reprStr->data;
}


/*
 * GetPartFilterExprNodeReprWalker is a helper function for GetPartFilterExprNodeRepr
 * to evaluate string representation of given node.
 */
static bool
GetPartFilterExprNodeReprWalker(Node *node, void *contextArg)
{
	CHECK_FOR_INTERRUPTS();

	if (node == NULL)
	{
		return false;
	}

	PartFilterExprNodeReprWalkerContext *context =
		(PartFilterExprNodeReprWalkerContext *) contextArg;

	/* use 4 spaces --not tabs-- per indentation level as Mongo does */
	StringInfo indentStr = makeStringInfo();
	appendStringInfoSpaces(indentStr, 4 * context->indentationLevel);

	if (IsA(node, BoolExpr))
	{
		PartFilterExprNodeReprWalkerContext childContext = {
			.indentationLevel = context->indentationLevel + 1,
			.reprStr = context->reprStr
		};

		BoolExpr *boolExpr = (BoolExpr *) node;
		if (boolExpr->boolop == AND_EXPR || boolExpr->boolop == OR_EXPR)
		{
			appendStringInfo(context->reprStr, "%s%s\n", indentStr->data,
							 boolExpr->boolop == AND_EXPR ? "$and" : "$or");
			return expression_tree_walker(node, GetPartFilterExprNodeReprWalker,
										  (void *) &childContext);
		}
		else if (boolExpr->boolop == NOT_EXPR)
		{
			/* check unexpected cases to be on the safe side */
			if (list_length(boolExpr->args) != 1)
			{
				ereport(ERROR, (errmsg("got unexpected number of args for "
									   "\"not\" operator")));
			}

			/*
			 * We expect to see CoalesceExpr nodes within "not" expressions,
			 * either for $nor operator or plain $not operator. Similarly,
			 * we don't expect to see those nodes in any other parts of the
			 * expression tree.
			 *
			 * For this reason; to be more specific, we check for CoalesceExpr
			 * nodes here, instead of doing so as part of top-level node checks.
			 */
			Node *notArg = linitial(boolExpr->args);
			if (!notArg || !IsA(notArg, CoalesceExpr))
			{
				ereport(ERROR, (errmsg("got unexpected node type as the "
									   "first argument of \"not\" operator, "
									   "expected CoalesceExpr")));
			}

			CoalesceExpr *coalesceExpr = (CoalesceExpr *) notArg;
			if (list_length(coalesceExpr->args) != 2)
			{
				ereport(ERROR, (errmsg("got unexpected number of args for "
									   "CoalesceExpr")));
			}

			appendStringInfo(context->reprStr, "%s$not\n", indentStr->data);

			/*
			 * CreateQualsFromQueryDocIterator places the actual expression
			 * to be negated for $not/$nor operators as the first argument of
			 * CoalesceExpr and we need to deparse that argument itself, not
			 * only its children; so shouldn't call expression_tree_walker
			 * here.
			 */
			Expr *innerExpr = linitial(coalesceExpr->args);
			return GetPartFilterExprNodeReprWalker((Node *) innerExpr,
												   (void *) &childContext);
		}
		else
		{
			ereport(ERROR, (errmsg("unknown bool operator")));
		}
	}
	else if (IsA(node, OpExpr) || IsA(node, FuncExpr))
	{
		const MongoQueryOperator *operator = NULL;
		List *args = NIL;
		operator = GetMongoQueryOperatorFromExpr(node, &args);

		if (operator->operatorType == QUERY_OPERATOR_UNKNOWN)
		{
			ereport(ERROR, (errmsg("unknown mongo operator")));
		}

		/* check unexpected cases to be on the safe side */
		if (list_length(args) != 2)
		{
			ereport(ERROR, (errmsg("got unexpected number of args for "
								   "operator")));
		}

		Node *rhsNode = lsecond(args);
		if (!rhsNode || !IsA(rhsNode, Const) ||
			!((Const *) rhsNode)->constvalue ||
			(((Const *) rhsNode)->consttype != BsonTypeId() &&
			 ((Const *) rhsNode)->consttype != BsonQueryTypeId()))
		{
			ereport(ERROR, (errmsg("got a non-Const node, or a null Const "
								   "value, or a non-bson Const node for "
								   "second argument of operator")));
		}

		Const *bsonConst = (Const *) rhsNode;

		pgbsonelement element;
		PgbsonToSinglePgbsonElement((pgbson *) bsonConst->constvalue, &element);

		appendStringInfo(context->reprStr, "%s%s %s %s\n",
						 indentStr->data, element.path, operator->mongoOperatorName,
						 BsonValueToJsonForLogging(&(element.bsonValue)));
	}

	return false;
}


/*
 * AcquireAdvisoryExclusiveLockForCreateIndexes acquires an advisory
 * ShareUpdateExclusiveLock for given collection. Note that the only reason for
 * acquiring a ShareUpdateExclusiveLock here is that it's the lowest lock
 * mode that cannot be acquired by more than one backend.
 *
 * This is mainly useful to prevent concurrent index metadata insertions
 * for given collection while checking for name / option conflicting indexes.
 *
 * Note that the lock type acquired by this function --advisory-- is different
 * than the usual collection lock that we acquire on the data table, so this
 * cannot anyhow conflict with a collection lock.
 *
 * Also note that we could instead acquire a "FOR UPDATE" lock on index metadata
 * based on given collectionId, but acquiring a "FOR UPDATE" lock on a Citus
 * reference table currently blocks writes for "x != a" as well when the where
 * clause is "x = a".
 */
void
AcquireAdvisoryExclusiveLockForCreateIndexes(uint64 collectionId)
{
	bool dontWait = false;
	bool sessionLock = false;
	LOCKTAG locktag;
	SET_LOCKTAG_ADVISORY(locktag, MyDatabaseId,
						 (collectionId & 0xFFFFFFFF),
						 (collectionId & (((uint64) 0xFFFFFFFF) << 32)) >> 32,
						 LT_FIELD4_EXCL_CREATE_INDEXES);
	(void) LockAcquire(&locktag, ShareUpdateExclusiveLock, sessionLock, dontWait);
}


/*
 * CheckForConflictsAndPruneExistingIndexes takes a list of IndexDef objects
 * and checks whether creating them would cause a name / option conflict with
 * another index.
 *
 * Throws an error if there is an index that conlicts with another one (that
 * already exists or that we're about to create) by name or index option.
 *
 * On success, prunes IndexDef objects that we should skip creating (due to
 * an already existing & identical index).
 */
List *
CheckForConflictsAndPruneExistingIndexes(uint64 collectionId, List *indexDefList)
{
	List *prunedIndexDefList = NIL;

	/*
	 * Drop IndexDef objects for the indexes that already exist and throw an
	 * error if any of them would cause a conflict with an already existing
	 * index.
	 */
	ListCell *indexDefCell = NULL;
	foreach(indexDefCell, indexDefList)
	{
		IndexDef *indexDef = lfirst(indexDefCell);

		IndexSpec indexSpec = MakeIndexSpecForIndexDef(indexDef);
		if (!CheckIndexSpecConflictWithExistingIndexes(collectionId, &indexSpec))
		{
			prunedIndexDefList = lappend(prunedIndexDefList, indexDef);
		}
	}

	/*
	 * And now we only consider the indexes that we're about to create and throw
	 * an error if one of them could cause a name / option conflict with another
	 * one within the list.
	 *
	 * Note that this must be done after prunning away the indexes that already
	 * exist (i.e.: after above loop) as Mongo does.
	 */
	for (int i = 0; i < list_length(prunedIndexDefList); i++)
	{
		const IndexSpec latterIndexSpec =
			MakeIndexSpecForIndexDef(list_nth(prunedIndexDefList, i));

		for (int j = 0; j < i; j++)
		{
			const IndexSpec priorIndexSpec =
				MakeIndexSpecForIndexDef(list_nth(prunedIndexDefList, j));

			bool indexNamesMatch =
				strcmp(priorIndexSpec.indexName, latterIndexSpec.indexName) == 0;

			IndexOptionsEquivalency indexSpecOptionsMatch =
				IndexSpecOptionsAreEquivalent(&priorIndexSpec, &latterIndexSpec);

			bool indexSpecTTLOptionsMatch =
				IndexSpecTTLOptionsAreSame(&priorIndexSpec, &latterIndexSpec);

			if (indexNamesMatch &&
				indexSpecOptionsMatch != IndexOptionsEquivalency_NotEquivalent)
			{
				/* all other options match, check expireAfterSeconds */
				if (!indexSpecTTLOptionsMatch ||
					indexSpecOptionsMatch == IndexOptionsEquivalency_Equivalent)
				{
					ThrowSameIndexNameWithDifferentOptionsError(
						&priorIndexSpec, &latterIndexSpec);
				}

				/*
				 * While Mongo simply skips creating identical indexes if the
				 * matching index was created before, it throws an hard error
				 * if the create_indexes() command itself attempts creating
				 * identical indexes.
				 */
				ereport(ERROR, (errcode(MongoIndexAlreadyExists),
								errmsg("Identical index already exists: %s",
									   priorIndexSpec.indexName)));
			}
			else if (indexNamesMatch &&
					 indexSpecOptionsMatch == IndexOptionsEquivalency_NotEquivalent)
			{
				ThrowIndexNameConflictError(&priorIndexSpec, &latterIndexSpec);
			}
			else if (!indexNamesMatch &&
					 indexSpecOptionsMatch != IndexOptionsEquivalency_NotEquivalent)
			{
				/* all other options match, check expireAfterSeconds */
				if (!indexSpecTTLOptionsMatch)
				{
					ThrowDifferentIndexNameWithDifferentOptionsError(
						&priorIndexSpec, &latterIndexSpec);
				}

				if (indexSpecOptionsMatch == IndexOptionsEquivalency_Equivalent)
				{
					ThrowDifferentIndexNameWithDifferentOptionsError(
						&priorIndexSpec, &latterIndexSpec);
				}

				ThrowIndexOptionsConflictError(priorIndexSpec.indexName);
			}
		}
	}

	return prunedIndexDefList;
}


/*
 * CheckIndexSpecConflictWithExistingIndexes queries index metadata for the
 * indexes created before and checks whether there is a a name / option
 * conflicting index.
 *
 * Returns true if there is an index with given name(*) and equivalent index
 * options; indicating that caller should skip creating this index.
 * (*): Note that for plain _id indexes, we assume both indexes are identical
 *      even if their names are different as Mongo does.
 *
 * Returns false if there is no name or option conflicting index; meaning
 * that it's safe to create given index.
 *
 * Finally, this function throws an error if there is an index that conlicts
 * with given one either by name or by index options.
 */
static bool
CheckIndexSpecConflictWithExistingIndexes(uint64 collectionId, const IndexSpec *indexSpec)
{
	const IndexDetails *nameMatchedIndexDetails =
		IndexNameGetIndexDetails(collectionId, indexSpec->indexName);
	if (nameMatchedIndexDetails == NULL)
	{
		const IndexDetails *optionsMatchedIndexDetails =
			FindIndexWithSpecOptions(collectionId, indexSpec);
		if (optionsMatchedIndexDetails != NULL)
		{
			/*
			 * If it's a plain _id index, then we assume both indexes are
			 * identical even if their names are different as Mongo does.
			 */
			IndexSpec idIndexSpec = MakeIndexSpecForBuiltinIdIndex();
			IndexOptionsEquivalency equivalency = IndexSpecOptionsAreEquivalent(indexSpec,
																				&
																				idIndexSpec);
			if (equivalency != IndexOptionsEquivalency_NotEquivalent)
			{
				return true;
			}

			/* all other options match, check expireAfterSeconds */
			if (!IndexSpecTTLOptionsAreSame(&optionsMatchedIndexDetails->indexSpec,
											indexSpec))
			{
				ThrowDifferentIndexNameWithDifferentOptionsError(
					&optionsMatchedIndexDetails->indexSpec, indexSpec);
			}

			equivalency = IndexSpecOptionsAreEquivalent(indexSpec,
														&optionsMatchedIndexDetails->
														indexSpec);

			if (equivalency == IndexOptionsEquivalency_TextEquivalent)
			{
				ThrowSingleTextIndexAllowedError(
					&optionsMatchedIndexDetails->indexSpec, indexSpec);
			}

			if (equivalency == IndexOptionsEquivalency_Equivalent)
			{
				ThrowDifferentIndexNameWithDifferentOptionsError(
					&optionsMatchedIndexDetails->indexSpec, indexSpec);
			}

			ThrowIndexOptionsConflictError(
				optionsMatchedIndexDetails->indexSpec.indexName);
		}

		return false;
	}


	if (IndexSpecOptionsAreEquivalent(&nameMatchedIndexDetails->indexSpec,
									  indexSpec) == IndexOptionsEquivalency_NotEquivalent)
	{
		ThrowIndexNameConflictError(&nameMatchedIndexDetails->indexSpec, indexSpec);
	}

	/* all other options match, check expireAfterSeconds */
	if (!IndexSpecTTLOptionsAreSame(&nameMatchedIndexDetails->indexSpec,
									indexSpec))
	{
		ThrowSameIndexNameWithDifferentOptionsError(&nameMatchedIndexDetails->indexSpec,
													indexSpec);
	}

	return true;
}


/*
 * ThrowIndexNameConflictError throws a MongoIndexKeySpecsConflict error
 * based on given IndexSpec's.
 */
static void
ThrowIndexNameConflictError(const IndexSpec *existingIndexSpec,
							const IndexSpec *requestedIndexSpec)
{
	const char *requestedIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(requestedIndexSpec));

	const char *existingIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(existingIndexSpec));

	ereport(ERROR, (errcode(MongoIndexKeySpecsConflict),
					errmsg("An existing index has the same name as the "
						   "requested index. When index names are not "
						   "specified, they are auto generated and can "
						   "cause conflicts. Please refer to our "
						   "documentation. Requested index: %s, existing "
						   "index: %s",
						   requestedIndexBsonStr, existingIndexBsonStr)));
}


/*
 * ThrowIndexOptionsConflictError throws a MongoIndexOptionsConflict error
 * using given index name.
 */
static void
ThrowIndexOptionsConflictError(const char *existingIndexName)
{
	ereport(ERROR, (errcode(MongoIndexOptionsConflict),
					errmsg("Index already exists with a different name: %s",
						   existingIndexName)));
}


/*
 * ThrowSameIndexNameWithDifferentOptionsError throws a
 * MongoIndexOptionsConflict error based on given IndexSpec's.
 *
 * Not every index option in Mongo makes two indexes different. That means,
 * the options that IndexSpecOptionsAreEquivalent() doesn't compare are not
 * expected to be different if IndexSpecOptionsAreEquivalent() returns true
 * for two indexes.
 *
 * This function is used to throw an error for such cases.
 */
static void
ThrowSameIndexNameWithDifferentOptionsError(const IndexSpec *existingIndexSpec,
											const IndexSpec *requestedIndexSpec)
{
	const char *requestedIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(requestedIndexSpec));

	const char *existingIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(existingIndexSpec));

	ereport(ERROR, (errcode(MongoIndexOptionsConflict),
					errmsg("An equivalent index already exists with the same "
						   "name but different options. Requested index: %s, "
						   "existing index: %s",
						   requestedIndexBsonStr, existingIndexBsonStr)));
}


/*
 * ThrowDifferentIndexNameWithDifferentOptionsError throws a
 * MongoIndexOptionsConflict error based on given IndexSpec's.
 *
 * This is similar to ThrowSameIndexNameWithDifferentOptionsError but used when
 * the index names are also different.
 */
static void
ThrowDifferentIndexNameWithDifferentOptionsError(const IndexSpec *existingIndexSpec,
												 const IndexSpec *requestedIndexSpec)
{
	const char *requestedIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(requestedIndexSpec));

	const char *existingIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(existingIndexSpec));

	ereport(ERROR, (errcode(MongoIndexOptionsConflict),
					errmsg("An equivalent index already exists with a "
						   "different name and options. Requested index: %s, "
						   "existing index: %s",
						   requestedIndexBsonStr, existingIndexBsonStr)));
}


static void
ThrowSingleTextIndexAllowedError(const IndexSpec *existingIndexSpec,
								 const IndexSpec *requestedIndexSpec)
{
	const char *requestedIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(requestedIndexSpec));

	const char *existingIndexBsonStr =
		PgbsonToJsonForLogging(IndexSpecAsBson(existingIndexSpec));

	ereport(ERROR, (errcode(MongoExactlyOneTextIndex),
					errmsg("Expected exactly one text index. Requested index: %s, "
						   "existing index: %s",
						   requestedIndexBsonStr, existingIndexBsonStr)));
}


/*
 * SetIndexesAsBuildInProgress marks given list of indexes as build-in-progress.
 * Returns true if all the indexes are marked as build-in-progress otherwise
 * false and sets "firstNotMarkedIndex" with the first indexId that can't be
 * marked as build-in-progress.
 *
 * In low level, this function acquires a ShareUpdateExclusiveLock based on the
 * lock tag determined by LockTagForInProgressIndexBuild, and IndexBuildIsInProgress
 * tries acquiring a ShareLock --without blocking-- based on the same lock tag.
 *
 * Given that ShareLock conflicts with ShareUpdateExclusiveLock but not with
 * another ShareLock; this allow multiple callers for IndexBuildIsInProgress but
 * a single one for SetIndexesAsBuildInProgress as there can only be a single
 * backend that is building the index, but multiple ones that might need to check
 * whether the index build is in-progress.
 */
static bool
SetIndexesAsBuildInProgress(List *indexIdList, int *firstNotMarkedIndex)
{
	ListCell *indexIdCell = NULL;
	foreach(indexIdCell, indexIdList)
	{
		int indexId = lfirst_int(indexIdCell);

		LOCKTAG locktag = LockTagForInProgressIndexBuild(indexId);
		bool sessionLock = true;

		/*
		 * We set dontWait to true and immediately return false if we can't
		 * acquire the lock.
		 */
		bool dontWait = true;
		if (LockAcquire(&locktag, ShareUpdateExclusiveLock, sessionLock, dontWait) ==
			LOCKACQUIRE_NOT_AVAIL)
		{
			*firstNotMarkedIndex = indexId;
			return false;
		}
	}
	return true;
}


/*
 * UnsetIndexesAsBuildInProgress marks given list of indexes as "not"
 * build-in-progress.
 *
 * SetIndexesAsBuildInProgress should already have been called for the same list
 * of indexes by this backend.
 */
static void
UnsetIndexesAsBuildInProgress(List *indexIdList)
{
	ListCell *indexIdCell = NULL;
	foreach(indexIdCell, indexIdList)
	{
		int indexId = lfirst_int(indexIdCell);

		LOCKTAG locktag = LockTagForInProgressIndexBuild(indexId);
		bool sessionLock = true;
		if (!LockRelease(&locktag, ShareUpdateExclusiveLock, sessionLock))
		{
			ereport(ERROR, (errmsg("index build for index %d was not in-progress",
								   indexId)));
		}
	}
}


/*
 * IndexBuildIsInProgress returns true if index build for the index with given
 * id is still in-progress.
 */
bool
IndexBuildIsInProgress(int indexId)
{
	LOCKTAG locktag = LockTagForInProgressIndexBuild(indexId);
	bool sessionLock = true;
	bool dontWait = true;
	bool canAcquireShareLock =
		LockAcquire(&locktag, ShareLock, sessionLock, dontWait) != LOCKACQUIRE_NOT_AVAIL;

	if (canAcquireShareLock)
	{
		/* and not hold it forever */
		(void) LockRelease(&locktag, ShareLock, sessionLock);

		/* When we can acquire lock index might still building by a background worker check for real state in the queue */
		IndexCmdStatus indexCmdStatus = GetIndexBuildStatusFromIndexQueue(indexId);
		return (indexCmdStatus != IndexCmdStatus_Unknown && indexCmdStatus !=
				IndexCmdStatus_Skippable);
	}

	/*
	 * We can't acquire ShareLock only if another session have already called
	 * SetIndexesAsBuildInProgress() for the same index, meaning that index build
	 * is still in-progress.
	 */
	return !canAcquireShareLock;
}


/*
 * LockTagForInProgressIndexBuild returns the locktag for the methods used to
 * determine whether index build for the index with given id is in-progress.
 */
static LOCKTAG
LockTagForInProgressIndexBuild(int indexId)
{
	LOCKTAG locktag;
	SET_LOCKTAG_ADVISORY(locktag, MyDatabaseId, indexId, 0,
						 LT_FIELD4_IN_PROG_INDEX_BUILD);

	/*
	 * Use DEFAULT_LOCKMETHOD so that an ereport(ERROR) call can automatically
	 * release the lock.
	 */
	locktag.locktag_lockmethodid = DEFAULT_LOCKMETHOD;

	return locktag;
}


/*
 * TryCreateCollectionIndexes tries creating indexes --with invalid
 * metadata entries first-- according to their definitions given by
 * indexDefList.
 *
 * Then sets metadata entries belonging to those indexes as valid before
 * returning if it can create all given indexes successfully.
 *
 * Returns a TryCreateIndexesResult object to report status of the overall
 * operation and never throws an error; except the case that it fails to
 * allocate memory for TryCreateIndexesResult object but this is fine.
 */
static TryCreateIndexesResult *
TryCreateCollectionIndexes(uint64 collectionId, List *indexDefList,
						   List *indexIdList, bool isUnsharded,
						   MemoryContext retValueContext)
{
	TryCreateIndexesResult *result = MemoryContextAllocZero(retValueContext,
															sizeof(TryCreateIndexesResult));

	int nindexes = list_length(indexDefList);
	if (nindexes == 0)
	{
		result->ok = true;
		return result;
	}

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile bool createdInvalidIndexes = false;
	PG_TRY();
	{
		ereport(DEBUG1, (errmsg("trying to create indexes and insert "
								"invalid metadata records for them")));

		TryCreateInvalidCollectionIndexes(collectionId, indexDefList,
										  indexIdList, isUnsharded);

		createdInvalidIndexes = true;
	}
	PG_CATCH();
	{
		/* save error info into right context */
		MemoryContextSwitchTo(retValueContext);
		ErrorData *edata = CopyErrorDataAndFlush();
		result->errcode = edata->sqlerrcode;
		result->errmsg = edata->message;

		ereport(DEBUG1, (errmsg("couldn't create some of the (invalid) "
								"collection indexes")));

		/*
		 * Couldn't complete creating invalid indexes, need to abort the
		 * outer transaction itself to fire abort handler.
		 */
		PopAllActiveSnapshots();
		AbortCurrentTransaction();
		StartTransactionCommand();
	}
	PG_END_TRY();

	if (!createdInvalidIndexes)
	{
		result->ok = false;
		return result;
	}

	/*
	 * Now try marking entries inserted for collection indexes as valid.
	 *
	 * Use a subtransaction	so that we can automatically rollback the changes
	 * made by MarkIndexesAsValid if something goes wrong when doing that.
	 */
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;
	BeginInternalSubTransaction(NULL);

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile bool markedIndexesAsValid = false;
	PG_TRY();
	{
		ereport(DEBUG1, (errmsg("trying to mark invalid indexes as valid")));

		if (MarkIndexesAsValid(collectionId, indexIdList) != list_length(indexIdList))
		{
			ereport(ERROR, (errcode(MongoIndexBuildAborted),
							errmsg(COLLIDX_CONCURRENTLY_DROPPED_RECREATED_ERRMSG)));
		}

		/*
		 * All done, commit the subtransaction and return to outer
		 * transaction context.
		 */
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		markedIndexesAsValid = true;
	}
	PG_CATCH();
	{
		/* save error info into right context */
		MemoryContextSwitchTo(retValueContext);
		ErrorData *edata = CopyErrorDataAndFlush();
		result->errcode = edata->sqlerrcode;
		result->errmsg = edata->message;

		ereport(DEBUG1, (errmsg("created invalid collection indexes but "
								"couldn't mark some of them as valid")));

		/*
		 * Abort the subtransaction to rollback any changes that
		 * MarkIndexesAsValid might have done.
		 */
		RollbackAndReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
	}
	PG_END_TRY();

	result->ok = markedIndexesAsValid;
	return result;
}


/*
 * TryCreateInvalidCollectionIndexes tries creating indexes according to
 * their definitions using given index ids and inserts invalid metadata
 * entries for those indexes.
 */
static void
TryCreateInvalidCollectionIndexes(uint64 collectionId, List *indexDefList,
								  List *indexIdList, bool isUnsharded)
{
	ListCell *indexDefCell = NULL;
	ListCell *indexIdCell = NULL;
	forboth(indexDefCell, indexDefList, indexIdCell, indexIdList)
	{
		IndexDef *indexDef = (IndexDef *) lfirst(indexDefCell);
		int indexId = lfirst_int(indexIdCell);

		/*
		 * We commit the transaction to prevent concurrent index creation
		 * getting blocked on that transaction due to any snapshots that
		 * we might have grabbed so far.
		 */
		PopAllActiveSnapshots();
		CommitTransactionCommand();
		StartTransactionCommand();

		/*
		 * Tell other backends to ignore us, even if we grab any
		 * snapshots later.
		 */
		set_indexsafe_procflags();

		bool createIndexesConcurrently = true;
		bool isTempCollection = false;
		CreatePostgresIndex(collectionId, indexDef, indexId, createIndexesConcurrently,
							isTempCollection, isUnsharded);
	}
}


/*
 * Tries rebuilding the postgres index one by one concurrently
 */
static TryReIndexesResult *
TryReIndexCollectionIndexesConcurrently(uint64 collectionId,
										List *indexesDetailList,
										List *indexIdList,
										MemoryContext retValueContext)
{
	TryReIndexesResult *tryResult = MemoryContextAllocZero(retValueContext,
														   sizeof(TryReIndexesResult));

	volatile bool reIndexedSuccessfully = false;
	PG_TRY();
	{
		ereport(DEBUG1, (errmsg("trying to reindex all the indexes of collection: "
								UINT64_FORMAT " concurrently",
								collectionId)));

		ListCell *indexDetailCell = NULL;
		bool concurrently = true;
		foreach(indexDetailCell, indexesDetailList)
		{
			/*
			 * We commit the transaction to prevent concurrent re-index creation
			 * getting blocked on that transaction due to any snapshots that
			 * we might have grabbed so far.
			 */
			PopAllActiveSnapshots();
			CommitTransactionCommand();
			StartTransactionCommand();

			/*
			 * Tell other backends to ignore us, even if we grab any
			 * snapshots later.
			 */
			set_indexsafe_procflags();

			IndexDetails *indexDetail = (IndexDetails *) lfirst(indexDetailCell);
			tryResult->indexCurrentlyBuilding = indexDetail;
			ReIndexPostgresIndex(collectionId, indexDetail, concurrently);
		}

		/* Reset indexCurrentlyBuilding to indicate no index is building now */
		tryResult->indexCurrentlyBuilding = NULL;
		reIndexedSuccessfully = true;
	}
	PG_CATCH();
	{
		/* save error info into right context */
		MemoryContextSwitchTo(retValueContext);
		ErrorData *edata = CopyErrorDataAndFlush();
		tryResult->errcode = edata->sqlerrcode;
		tryResult->errmsg = edata->message;

		ereport(WARNING, (errmsg("couldn't reIndex collection index. Reason: %s",
								 edata->message)));

		/*
		 * Couldn't complete reindexing, need to abort the
		 * outer transaction itself to fire abort handler.
		 */
		PopAllActiveSnapshots();
		AbortCurrentTransaction();

		if (edata->sqlerrcode == ERRCODE_READ_ONLY_SQL_TRANSACTION)
		{
			ReThrowError(edata);
		}

		StartTransactionCommand();
	}
	PG_END_TRY();
	tryResult->ok = reIndexedSuccessfully;
	return tryResult;
}


/*
 * MakeIndexSpecForIndexDef creates an IndexSpec object based on given IndexDef.
 */
IndexSpec
MakeIndexSpecForIndexDef(IndexDef *indexDef)
{
	IndexSpec indexSpec = {
		.indexName = indexDef->name,
		.indexVersion = indexDef->version,
		.indexKeyDocument = indexDef->keyDocument,
		.indexPFEDocument = indexDef->partialFilterExprDocument,
		.indexWPDocument = indexDef->wildcardProjectionDocument,
		.indexSparse = indexDef->sparse,
		.indexUnique = indexDef->unique,
		.cosmosSearchOptions = NULL,
		.indexOptions = NULL
	};

	if (indexDef->cosmosSearchOptions != NULL)
	{
		indexSpec.cosmosSearchOptions = indexDef->cosmosSearchOptions->searchOptionsDoc;
	}

	/* Populate indexOptions */
	/* As you update this, make sure to make IndexSpecOptionsAreEquivalent consistent */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	if (indexDef->defaultLanguage != NULL)
	{
		PgbsonWriterAppendUtf8(&writer, "default_language", -1,
							   indexDef->defaultLanguage);
	}

	if (indexDef->languageOverride != NULL)
	{
		PgbsonWriterAppendUtf8(&writer, "language_override", -1,
							   indexDef->languageOverride);
	}

	if (indexDef->weightsDocument != NULL)
	{
		PgbsonWriterAppendDocument(&writer, "weights", 7,
								   indexDef->weightsDocument);
	}

	if (indexDef->minBound != NULL)
	{
		PgbsonWriterAppendDouble(&writer, "min", 3,
								 *indexDef->minBound);
	}

	if (indexDef->maxBound != NULL)
	{
		PgbsonWriterAppendDouble(&writer, "max", 3,
								 *indexDef->maxBound);
	}

	if (indexDef->bits > 0)
	{
		PgbsonWriterAppendDouble(&writer, "bits", 4,
								 indexDef->bits);
	}

	if (indexDef->sphereIndexVersion > 0)
	{
		PgbsonWriterAppendInt32(&writer, "2dsphereIndexVersion", 20,
								indexDef->sphereIndexVersion);
	}

	if (indexDef->finestIndexedLevel != NULL)
	{
		PgbsonWriterAppendInt32(&writer, "finestIndexedLevel", 18,
								*indexDef->finestIndexedLevel);
	}

	if (indexDef->coarsestIndexedLevel != NULL)
	{
		PgbsonWriterAppendInt32(&writer, "coarsestIndexedLevel", 20,
								*indexDef->coarsestIndexedLevel);
	}

	if (indexDef->enableLargeIndexKeys == BoolIndexOption_True)
	{
		PgbsonWriterAppendInt32(&writer, "enableLargeIndexKeys", 20, 1);
	}

	if (!IsPgbsonWriterEmptyDocument(&writer))
	{
		indexSpec.indexOptions = PgbsonWriterGetPgbson(&writer);
	}

	if (indexDef->expireAfterSeconds != NULL)
	{
		indexSpec.indexExpireAfterSeconds = palloc0(sizeof(int));
		*indexSpec.indexExpireAfterSeconds = *indexDef->expireAfterSeconds;
	}

	return indexSpec;
}


/*
 * CreatePostgresIndex creates RUM index based on given IndexDef object using
 * given Mongo index id.
 */
static void
CreatePostgresIndex(uint64 collectionId, IndexDef *indexDef, int indexId,
					bool concurrently, bool isTempCollection, bool isUnsharded)
{
	char *cmd = CreatePostgresIndexCreationCmd(collectionId, indexDef, indexId,
											   concurrently, isTempCollection);
	const Oid userOid = InvalidOid;
	bool useSerialExecution = isUnsharded;
	ExecuteCreatePostgresIndexCmd(cmd, concurrently, userOid, useSerialExecution);
}


/*
 * CreatePostgresIndexCreationCmd creates postgres index creation command based on indexDef passed.
 */
char *
CreatePostgresIndexCreationCmd(uint64 collectionId, IndexDef *indexDef, int indexId,
							   bool concurrently, bool isTempCollection)
{
	/*
	 * TODO: "For a compound multikey index, each indexed document can have at
	 *       most one indexed field whose value is an array".
	 *       Need to ensure this is the case when building the index.
	 *
	 * TODO: Currently we don't know how to build unique RUM indexes concurrently.
	 */

	StringInfo cmdStr = makeStringInfo();
	bool unique = indexDef->unique == BoolIndexOption_True;
	bool sparse = indexDef->sparse == BoolIndexOption_True;
	if (unique)
	{
		if (isTempCollection)
		{
			appendStringInfo(cmdStr,
							 "ALTER TABLE documents_temp");
		}
		else
		{
			appendStringInfo(cmdStr,
							 "ALTER TABLE %s." MONGO_DATA_TABLE_NAME_FORMAT,
							 ApiDataSchemaName, collectionId);
		}

		if (indexDef->enableLargeIndexKeys == BoolIndexOption_True)
		{
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"enableLargeIndexKeys with unique indexes is not supported yet")));
		}

		bool enableLargeIndexKeys = false;
		appendStringInfo(cmdStr,
						 " ADD CONSTRAINT " MONGO_DATA_TABLE_INDEX_NAME_FORMAT
						 " EXCLUDE USING %s_rum (%s) %s%s%s",
						 indexId, ExtensionObjectPrefix,
						 GenerateIndexExprStr(unique, sparse, indexDef->key,
											  indexDef->wildcardProjectionTree,
											  indexDef->name,
											  indexDef->defaultLanguage,
											  indexDef->languageOverride,
											  enableLargeIndexKeys),
						 indexDef->partialFilterExpr ? "WHERE (" : "",
						 indexDef->partialFilterExpr ?
						 GenerateIndexFilterStr(collectionId,
												indexDef->partialFilterExpr) :
						 "",
						 indexDef->partialFilterExpr ? ")" : "");
	}
	else if (indexDef->cosmosSearchOptions != NULL)
	{
		if (indexDef->cosmosSearchOptions->indexKind ==
			MongoCdbIndexKind_VectorSearch_Ivf)
		{
			int numLists = indexDef->cosmosSearchOptions->vectorOptions.numLists;

			/* Limitation/Todo: pgvector does not support concurrent index creation */
			/* It currently fails if we try to create index concurrently when the collection has data. */
			appendStringInfo(cmdStr,
							 "CREATE INDEX " MONGO_DATA_TABLE_INDEX_NAME_FORMAT
							 " ON %s." MONGO_DATA_TABLE_NAME_FORMAT
							 " USING ivfflat(%s) WITH (lists = %d)",
							 indexId, ApiDataSchemaName, collectionId,
							 GenerateVectorIndexExprStr(indexDef->key,
														indexDef->cosmosSearchOptions),
							 numLists);

			/* Add WHERE bson_extract_vector(document, path) IS NOT NULL predicate to allow search queries with the same clause to use the index */
			IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) linitial(
				indexDef->key->keyPathList);
			char *keyPath = (char *) indexKeyPath->path;

			appendStringInfo(cmdStr,
							 " WHERE %s.bson_extract_vector(document, %s::text) IS NOT NULL",
							 ApiCatalogSchemaName, quote_literal_cstr(keyPath));
		}
		else if (indexDef->cosmosSearchOptions->indexKind ==
				 MongoCdbIndexKind_VectorSearch_Hnsw)
		{
			int m = indexDef->cosmosSearchOptions->vectorOptions.m;
			int efConstruction =
				indexDef->cosmosSearchOptions->vectorOptions.efConstruction;

			appendStringInfo(cmdStr,
							 "CREATE INDEX " MONGO_DATA_TABLE_INDEX_NAME_FORMAT
							 " ON %s." MONGO_DATA_TABLE_NAME_FORMAT
							 " USING hnsw(%s) WITH (m = %d, ef_construction = %d)",
							 indexId, ApiDataSchemaName, collectionId,
							 GenerateVectorIndexExprStr(indexDef->key,
														indexDef->cosmosSearchOptions),
							 m, efConstruction);

			/* Add WHERE bson_extract_vector(document, path) IS NOT NULL predicate to allow search queries with the same clause to use the index */
			IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) linitial(
				indexDef->key->keyPathList);
			char *keyPath = (char *) indexKeyPath->path;

			appendStringInfo(cmdStr,
							 " WHERE %s.bson_extract_vector(document, %s::text) IS NOT NULL",
							 ApiCatalogSchemaName, quote_literal_cstr(keyPath));
		}
		else
		{
			if (!EnableVectorHNSWIndex)
			{
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg(
									"cosmosSearch index only supports 'vector-ivf'")));
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg(
									"cosmosSearch index only supports 'vector-ivf' or 'vector-hnsw'")));
			}
		}
	}
	else if (indexDef->key->has2dIndex)
	{
		/*
		 * Mongo `2d` indexes work on planar coordinate pairs, these are always sparse and
		 * ignore any sparse options passed in.
		 *
		 * PostGIS can create indexes for 2d, 3d and even 4d spatial points but since mongo
		 * only uses 2d space we restrict the behavior to it.
		 *
		 * Since Mongo 2d indexes are `sparse` in nature, we make sure that our 2d index is also sparse
		 * by adding this `ApiCatalogSchemaName.bson_validate_geometry(document, %s::text) IS NOT NULL` predicate
		 * in the CREATE INDEX statement, this adds an overhead for us to match this predicate in our query'
		 * but postgres has a special case for `IS NULL` statements, if the functions are `strict` then they
		 * simply pass the `IS NULL` predicate of CREATE INDEX.
		 * More info: https://github.com/postgres/postgres/blob/617f9b7d4b10fec00a86802eeb34d7295c52d747/src/backend/optimizer/util/predtest.c#L1182
		 *
		 */
		IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) linitial(
			indexDef->key->keyPathList);
		char *keyPath = (char *) indexKeyPath->path;
		double minBound = indexDef->minBound ? *indexDef->minBound :
						  DEFAULT_2D_INDEX_MIN_BOUND;
		double maxBound = indexDef->maxBound ? *indexDef->maxBound :
						  DEFAULT_2D_INDEX_MAX_BOUND;
		appendStringInfo(cmdStr,
						 "CREATE INDEX %s " MONGO_DATA_TABLE_INDEX_NAME_FORMAT
						 " ON %s." MONGO_DATA_TABLE_NAME_FORMAT
						 " USING GIST( %s.bson_validate_geometry(document, %s::text) "
						 "%s.bson_gist_geometry_ops_2d(path=%s, minbound=%g, maxbound=%g) )"
						 " WHERE %s.bson_validate_geometry(document, %s::text) IS NOT NULL %s%s%s",
						 concurrently ? "CONCURRENTLY" : "", indexId,
						 ApiDataSchemaName, collectionId, ApiCatalogSchemaName,
						 quote_literal_cstr(keyPath), ApiCatalogSchemaName,
						 quote_literal_cstr(keyPath),
						 minBound, maxBound, ApiCatalogSchemaName,
						 quote_literal_cstr(keyPath),
						 indexDef->partialFilterExpr ? "AND (" : "",
						 indexDef->partialFilterExpr ?
						 GenerateIndexFilterStr(collectionId,
												indexDef->partialFilterExpr) : "",
						 indexDef->partialFilterExpr ? ")" : "");
	}
	else if (indexDef->key->has2dsphereIndex)
	{
		appendStringInfo(cmdStr,
						 "CREATE INDEX %s " MONGO_DATA_TABLE_INDEX_NAME_FORMAT
						 " ON %s." MONGO_DATA_TABLE_NAME_FORMAT
						 " USING GIST(%s) WHERE (%s)"
						 " %s%s%s",
						 concurrently ? "CONCURRENTLY" : "", indexId,
						 ApiDataSchemaName, collectionId,
						 Generate2dsphereIndexExprStr(indexDef->key),
						 Generate2dsphereSparseExprStr(indexDef->key),
						 indexDef->partialFilterExpr ? " AND (" : "",
						 indexDef->partialFilterExpr ?
						 GenerateIndexFilterStr(collectionId,
												indexDef->partialFilterExpr) : "",
						 indexDef->partialFilterExpr ? ")" : "");
	}
	else
	{
		appendStringInfo(cmdStr,
						 "CREATE INDEX %s " MONGO_DATA_TABLE_INDEX_NAME_FORMAT,
						 concurrently ? "CONCURRENTLY" : "", indexId);

		if (isTempCollection)
		{
			appendStringInfo(cmdStr,
							 " ON documents_temp");
		}
		else
		{
			appendStringInfo(cmdStr,
							 " ON %s." MONGO_DATA_TABLE_NAME_FORMAT,
							 ApiDataSchemaName, collectionId);
		}

		bool enableLargeIndexKeys = DefaultEnableLargeIndexKeys;
		if (indexDef->enableLargeIndexKeys != BoolIndexOption_Undefined)
		{
			enableLargeIndexKeys = indexDef->enableLargeIndexKeys == BoolIndexOption_True;
		}

		appendStringInfo(cmdStr,
						 " USING %s_rum (%s) %s%s%s",
						 ExtensionObjectPrefix,
						 GenerateIndexExprStr(unique, sparse, indexDef->key,
											  indexDef->wildcardProjectionTree,
											  indexDef->name,
											  indexDef->defaultLanguage,
											  indexDef->languageOverride,
											  enableLargeIndexKeys),
						 indexDef->partialFilterExpr ? "WHERE (" : "",
						 indexDef->partialFilterExpr ?
						 GenerateIndexFilterStr(collectionId,
												indexDef->partialFilterExpr) :
						 "",
						 indexDef->partialFilterExpr ? ")" : "");
	}
	return cmdStr->data;
}


/*
 * ExecuteCreatePostgresIndexCmd executes the index creation postgres command.
 */
void
ExecuteCreatePostgresIndexCmd(char *cmd, bool concurrently, const Oid userOid,
							  bool useSerialExecution)
{
	if (concurrently)
	{
		ExtensionExecuteQueryAsUserOnLocalhostViaLibPQ(cmd, userOid, useSerialExecution);
	}
	else
	{
		if (userOid != InvalidOid)
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg("Create index failed due to incorrect userid"),
							errhint("Create index failed due to incorrect userid")));
		}

		bool readOnly = false;
		bool isNull = false;
		if (useSerialExecution)
		{
			RunQueryWithSequentialModification(cmd, SPI_OK_UTILITY, &isNull);
		}
		else
		{
			ExtensionExecuteQueryViaSPI(cmd, readOnly, SPI_OK_UTILITY, &isNull);
		}

		Assert(isNull);
	}
}


/*
 * Generates the 2dsphere Index expression for multiple index fields.
 */
static char *
Generate2dsphereIndexExprStr(const IndexDefKey *indexDefKey)
{
	int keysLength = list_length(indexDefKey->keyPathList);
	Assert(keysLength > 0);
	StringInfo sphereIndexExpr = makeStringInfo();
	int index = 0;
	ListCell *keyPathCell = NULL;
	foreach(keyPathCell, indexDefKey->keyPathList)
	{
		IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) lfirst(keyPathCell);
		const char *quotedPath = quote_literal_cstr(indexKeyPath->path);
		appendStringInfo(sphereIndexExpr,
						 "%s.bson_validate_geography(document, %s::text) "
						 "%s.bson_gist_geography_ops_2d( path=%s ) ",
						 ApiCatalogSchemaName, quotedPath, ApiCatalogSchemaName,
						 quotedPath);
		index++;
		if (index < keysLength)
		{
			appendStringInfoChar(sphereIndexExpr, ',');
		}
	}
	return sphereIndexExpr->data;
}


/*
 * Generates the 2dsphere Index Sparse expression for multiple index fields.
 */
static char *
Generate2dsphereSparseExprStr(const IndexDefKey *indexDefKey)
{
	int keysLength = list_length(indexDefKey->keyPathList);
	Assert(keysLength > 0);
	StringInfo sphereIndexExpr = makeStringInfo();
	int index = 0;
	ListCell *keyPathCell = NULL;
	foreach(keyPathCell, indexDefKey->keyPathList)
	{
		IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) lfirst(keyPathCell);
		appendStringInfo(sphereIndexExpr,
						 " %s.bson_validate_geography(document, %s::text)"
						 " IS NOT NULL ",
						 ApiCatalogSchemaName,
						 quote_literal_cstr(indexKeyPath->path));
		index++;
		if (index < keysLength)
		{
			appendStringInfo(sphereIndexExpr, "%s", "OR");
		}
	}
	return sphereIndexExpr->data;
}


/*
 * ReIndexPostgresIndex reindexes the given collection index
 * in postgres.
 * indexDetail => IndexDetails of the index to be rebuild
 * concurrently => Whether or not to reindex concurrently
 *
 *
 * Note:
 * 1- REINDEX CONCURRENTLY is performed using libpq connection to localhost and REINDEX (without CONCURRENTLY) is
 *    executed in SPI because we can't use the CONCURRENTLY option within a transaction block. More information
 *    can be found in docs/indexing/index_builds_and_metadata.md.
 *
 * 2- Executing queries using libPQ connection can cause undetectable deadlocks which is explained in
 *    depth in `ExtensionExecuteQueryOnLocalhostViaLibPQ` method documentation in "include/utils/query_utils.h".
 *
 * 3- Even when concurrently is true, unique indexes are not rebuild concurrently.
 *    As of now we don't know how to reindex exclusion index concurrently because postgres
 *    doesn't allow this.
 */
static void
ReIndexPostgresIndex(uint64 collectionId, IndexDetails *indexDetail, bool concurrently)
{
	bool readOnly = false;
	bool isNull = false;

	bool isIdIndex = (strncmp(indexDetail->indexSpec.indexName,
							  ID_INDEX_NAME, strlen(ID_INDEX_NAME)) == 0);

	StringInfo cmdStr = makeStringInfo();
	if (indexDetail->indexSpec.indexUnique == BoolIndexOption_True || !concurrently)
	{
		/* ReBuild the index non-concurrently if index is unique or concurrently is not requested */
		appendStringInfo(cmdStr, "REINDEX INDEX %s.", ApiDataSchemaName);
		if (isIdIndex)
		{
			appendStringInfo(cmdStr, MONGO_DATA_PRIMARY_KEY_FORMAT_PREFIX UINT64_FORMAT,
							 collectionId);
		}
		else
		{
			appendStringInfo(cmdStr, MONGO_DATA_TABLE_INDEX_NAME_FORMAT,
							 indexDetail->indexId);
		}

		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
									SPI_OK_UTILITY, &isNull);
		Assert(isNull);

		ereport(DEBUG1, (errmsg("Non-concurrent index rebuilt is successful - "
								MONGO_DATA_TABLE_INDEX_NAME_FORMAT,
								indexDetail->indexId)));
	}
	else
	{
		/* Build the index concurrently */
		appendStringInfo(cmdStr, "REINDEX INDEX CONCURRENTLY %s.", ApiDataSchemaName);
		if (isIdIndex)
		{
			appendStringInfo(cmdStr, MONGO_DATA_PRIMARY_KEY_FORMAT_PREFIX UINT64_FORMAT,
							 collectionId);
		}
		else
		{
			appendStringInfo(cmdStr, MONGO_DATA_TABLE_INDEX_NAME_FORMAT,
							 indexDetail->indexId);
		}

		/* Concurrent index rebuild is done via libpq in a remote connection similar to create_index */
		ExtensionExecuteQueryOnLocalhostViaLibPQ(cmdStr->data);
	}
}


/*
 * ResolveWPPathOpsFromTree takes a tree that represents "wildcardProjection"
 * document and returns a WildcardProjectionPathOps object that contains params
 * for bson_rum_wildcard_project_path_ops().
 */
static WildcardProjectionPathOps *
ResolveWPPathOpsFromTree(const BsonIntermediatePathNode *wildcardProjectionTree)
{
	WildcardProjectionPathOps *wpPathOps = palloc0(sizeof(WildcardProjectionPathOps));

	/*
	 * Note that here we pass first child of wildcardProjectionTree to skip
	 * dummy root node.
	 */
	bool isTopLevel = true;
	List *pathFieldList = NIL;
	ResolveWPPathOpsFromTreeInternal(
		wildcardProjectionTree,
		isTopLevel,
		pathFieldList, &wpPathOps->nonIdFieldPathList,
		&wpPathOps->nonIdFieldInclusion,
		&wpPathOps->idFieldInclusion);

	return wpPathOps;
}


/*
 * ResolveWPPathOpsFromTreeInternal is the internal function to
 * ResolveWPPathOpsFromTree to be used when recursively traversing nodes of
 * given tree.
 *
 * isTopLevel should be set to false in recursive calls and should only be
 * set to true by ResolveWPPathOpsFromTree.
 *
 * pathFieldList keeps track of the full-path down to a leaf node and should
 * be passed as an empty list for the top-level call.
 *
 * ** Output args: **
 *
 * nonIdFieldPathList is the list used to collect non-id field paths and should
 * be set to NIL for the top-level call.
 *
 * nonIdFieldInclusion and idFieldInclusion are set to report inclusion
 * specifications for the related fields.
 */
static void
ResolveWPPathOpsFromTreeInternal(const BsonIntermediatePathNode *treeParentNode,
								 bool isTopLevel, List *pathFieldList,
								 List **nonIdFieldPathList,
								 WildcardProjFieldInclusionMode *nonIdFieldInclusion,
								 WildcardProjFieldInclusionMode *idFieldInclusion)
{
	check_stack_depth();

	Assert(!isTopLevel || (pathFieldList == NIL &&
						   *nonIdFieldInclusion == WP_IM_INVALID &&
						   *idFieldInclusion == WP_IM_INVALID));

	const BsonPathNode *treeNode;
	foreach_child(treeNode, treeParentNode)
	{
		CHECK_FOR_INTERRUPTS();

		switch (treeNode->nodeType)
		{
			case NodeType_Intermediate:
			{
				const BsonIntermediatePathNode *intermediateNode = CastAsIntermediateNode(
					treeNode);
				if (intermediateNode->hasExpressionFieldsInChildren)
				{
					ereport(ERROR, (errmsg("unexpectedly got an Intermediate tree "
										   "node with field when traversing the "
										   "(internal) tree representation of "
										   "'wildcardProjection' specification")));
				}

				if (!IntermediateNodeHasChildren(intermediateNode))
				{
					ereport(ERROR, (errmsg("unexpectedly got an Intermediate "
										   "tree node that has no children when "
										   "traversing the (internal) tree "
										   "representation of 'wildcardProjection' "
										   "specification")));
				}

				char *fieldName = CreateStringFromStringView(&treeNode->field);
				List *childPathFieldList = lappend(list_copy(pathFieldList), fieldName);
				bool recurseIsTopLevel = false;
				ResolveWPPathOpsFromTreeInternal(
					intermediateNode,
					recurseIsTopLevel,
					childPathFieldList, nonIdFieldPathList,
					nonIdFieldInclusion, idFieldInclusion);

				break;
			}

			case NodeType_LeafIncluded:
			case NodeType_LeafExcluded:
			{
				WildcardProjFieldInclusionMode nodeInclusionMode =
					(treeNode->nodeType == NodeType_LeafIncluded) ? WP_IM_INCLUDE :
					WP_IM_EXCLUDE;

				char *fieldName = CreateStringFromStringView(&treeNode->field);
				List *leafPathFieldList = lappend(list_copy(pathFieldList), fieldName);
				char *fieldFullPath = StringListJoin(leafPathFieldList, ".");

				if (isTopLevel &&
					StringViewEquals(&treeNode->field, &IdFieldStringView))
				{
					*idFieldInclusion = nodeInclusionMode;
				}
				else
				{
					*nonIdFieldInclusion = nodeInclusionMode;

					*nonIdFieldPathList = lappend(*nonIdFieldPathList, fieldFullPath);
				}

				break;
			}

			default:
			{
				ereport(ERROR, (errmsg("got unexpected tree node type when "
									   "traversing the (internal) tree "
									   "representation of 'wildcardProjection' "
									   "specification")));
			}
		}
	}
}


/*
 * Generates the Index expression for the vector index column
 */
static char *
GenerateVectorIndexExprStr(IndexDefKey *indexDefKey,
						   const CosmosSearchOptions *searchOptions)
{
	StringInfo indexExprStr = makeStringInfo();

	IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) linitial(
		indexDefKey->keyPathList);
	char *keyPath = (char *) indexKeyPath->path;

	char *options;
	switch (searchOptions->vectorOptions.distanceMetric)
	{
		case VectorIndexDistanceMetric_IPDistance:
		{
			options = "vector_ip_ops";
			break;
		}

		case VectorIndexDistanceMetric_CosineDistance:
		{
			options = "vector_cosine_ops";
			break;
		}

		case VectorIndexDistanceMetric_L2Distance:
		default:
		{
			options = "vector_l2_ops";
			break;
		}
	}

	appendStringInfo(indexExprStr,
					 "CAST(%s.bson_extract_vector(document, %s::text) AS public.vector(%d)) public.%s",
					 ApiCatalogSchemaName, quote_literal_cstr(keyPath),
					 searchOptions->vectorOptions.numDimensions,
					 options);
	return indexExprStr->data;
}


/*
 * GenerateIndexExprStr returns column expression string to be used when
 * creating the index whose "key" and "wildcardProjection" specifications
 * are determined by given objects.
 *
 * indexDefWildcardProjTree should be passed to be NULL if index doesn't
 * have a "wildcardProjection" specification.
 */
static char *
GenerateIndexExprStr(bool unique, bool sparse, IndexDefKey *indexDefKey,
					 const BsonIntermediatePathNode *indexDefWildcardProjTree,
					 const char *indexName, const char *defaultLanguage,
					 const char *languageOverride, bool enableLargeIndexKeys)
{
	StringInfo indexExprStr = makeStringInfo();


	char *languageOptionKey = "";
	char *languageOptionValue = "";
	char *languageOverrideKey = "";
	char *languageOverrideValue = "";
	if (defaultLanguage != NULL)
	{
		languageOptionKey = ",defaultlanguage=";
		languageOptionValue = quote_literal_cstr(defaultLanguage);
	}

	if (languageOverride != NULL)
	{
		languageOverrideKey = ",languageOverride=";
		languageOverrideValue = quote_literal_cstr(languageOverride);
	}

	bool firstColumnWritten = false;
	char indexTermSizeLimitArg[22] = { 0 };

	if (list_length(indexDefKey->keyPathList) == 0)
	{
		if (!indexDefKey->isWildcard)
		{
			ereport(ERROR, (errmsg("unexpectedly got empty index key list")));
		}

		if (unique)
		{
			/* This should have been validated but do one more sanity check */
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg("Cannot create wildcard unique indexes")));
		}

		if (enableLargeIndexKeys || ForceIndexTermTruncation)
		{
			sprintf(indexTermSizeLimitArg, ",tl=%u",
					ComputeIndexTermLimit(SINGLE_PATH_INDEX_TERM_SIZE_LIMIT));
		}

		if (indexDefKey->hasTextIndexes)
		{
			/* No pathkeys + text index is a text index on the root */
			appendStringInfo(indexExprStr,
							 "%s document %s.bson_rum_text_path_ops"
							 "(iswildcard=true, weights=%s%s%s%s%s)",
							 firstColumnWritten ? "," : "",
							 ApiCatalogSchemaName,
							 quote_literal_cstr(SerializeWeightedPaths(
													indexDefKey->textPathList)),
							 languageOptionKey, languageOptionValue,
							 languageOverrideKey, languageOverrideValue);

			firstColumnWritten = true;
		}
		else if (!indexDefWildcardProjTree)
		{
			appendStringInfo(indexExprStr,
							 "%s document %s.bson_rum_single_path_ops"
							 "(path='', iswildcard=true%s)",
							 firstColumnWritten ? "," : "",
							 ApiCatalogSchemaName,
							 indexTermSizeLimitArg);

			firstColumnWritten = true;
		}
		else
		{
			WildcardProjectionPathOps *wpPathOps =
				ResolveWPPathOpsFromTree(indexDefWildcardProjTree);
			if (wpPathOps->nonIdFieldInclusion == WP_IM_INVALID &&
				wpPathOps->idFieldInclusion == WP_IM_INVALID)
			{
				ereport(ERROR, (errmsg("unexpectedly got empty "
									   "\"wildcardProjection\" specification")));
			}

			/*
			 * Default behavior for "_id" field inclusion in Mongo is "exclude",
			 * so need to set "includeid" to false if it hasn't been specified
			 * at all.
			 */
			bool includeId = wpPathOps->idFieldInclusion == WP_IM_INCLUDE;
			appendStringInfo(indexExprStr,
							 "%s document %s.bson_rum_wildcard_project_path_ops"
							 "(includeid=%s%s",
							 firstColumnWritten ? "," : "",
							 ApiCatalogSchemaName,
							 includeId ? "true" : "false",
							 indexTermSizeLimitArg);

			firstColumnWritten = true;

			if (wpPathOps->nonIdFieldInclusion != WP_IM_INVALID)
			{
				appendStringInfo(indexExprStr,
								 ", pathspec=%s, isexclusion=%s)",
								 quote_literal_cstr(
									 StringListGetBsonArrayRepr(
										 wpPathOps->nonIdFieldPathList)),
								 wpPathOps->nonIdFieldInclusion == WP_IM_EXCLUDE ?
								 "true" : "false");
			}
			else
			{
				appendStringInfoChar(indexExprStr, ')');
			}
		}

		/* From Ad-hoc tests, Postgres crashes if the index options becomes too long. Based on data, it seems having more than 2000 characters
		 * in the index options etc, seems to crash the entire thing. To protect from backend crashes, enforce a limit here
		 * Crash is on https://github.com/postgres/postgres/blob/master/src/backend/access/index/indexam.c#L556C9-L556C27
		 */
		if (indexExprStr->len >= MAX_INDEX_OPTIONS_LENGTH)
		{
			int lengthDelta = indexExprStr->len - MAX_INDEX_OPTIONS_LENGTH;
			ereport(ERROR, (errcode(MongoCannotCreateIndex),
							errmsg(
								"The index path or expression is too long. Try a shorter path or reducing paths by %d characters.",
								lengthDelta),
							errhint(
								"The index path or expression is too long. Try a shorter path or reducing paths by %d characters.",
								lengthDelta)));
		}
	}
	else
	{
		if (indexDefWildcardProjTree)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"unexpectedly got wildcardProjection "
								"specification for a non-wildcard index "
								"or a non-root wildcard index")));
		}

		/* We can't truncate terms on unique indexes as that would break uniqueness checks. */
		if (!unique && (enableLargeIndexKeys || ForceIndexTermTruncation))
		{
			uint32_t indexTermSizeLimit = list_length(indexDefKey->keyPathList) > 1 ?
										  COMPOUND_INDEX_TERM_SIZE_LIMIT :
										  SINGLE_PATH_INDEX_TERM_SIZE_LIMIT;
			sprintf(indexTermSizeLimitArg, ",tl=%u", ComputeIndexTermLimit(
						indexTermSizeLimit));
		}

		ListCell *keyPathCell = NULL;
		foreach(keyPathCell, indexDefKey->keyPathList)
		{
			int32_t currentExprLength = indexExprStr->len;
			IndexDefKeyPath *indexKeyPath = (IndexDefKeyPath *) lfirst(keyPathCell);
			char *keyPath = (char *) indexKeyPath->path;

			switch (indexKeyPath->indexKind)
			{
				case MongoIndexKind_Regular:
				{
					appendStringInfo(indexExprStr,
									 "%s document %s.bson_rum_single_path_ops(path=%s%s%s%s)",
									 firstColumnWritten ? "," : "",
									 ApiCatalogSchemaName,
									 quote_literal_cstr(keyPath),
									 indexKeyPath->isWildcard ? ",iswildcard=true" : "",
									 indexTermSizeLimitArg,

					                 /* We generate a term for paths that are not found only in the case of
					                  * unique non-sparse indexes */
									 unique && !sparse ? ", generateNotFoundTerm=true" :
									 "");
					if (unique)
					{
						appendStringInfo(indexExprStr, " WITH OPERATOR(%s.=?=)",
										 ApiCatalogSchemaName);

						/* Add a unique hash path for this column that includes the shard key */
						appendStringInfo(indexExprStr,
										 ", ((shard_key_value, document)::%s.shard_key_and_document) "
										 "%s.bson_rum_exclusion_ops(path=%s) WITH OPERATOR(%s.=)",
										 ApiCatalogSchemaName, ApiCatalogSchemaName,
										 quote_literal_cstr(keyPath),
										 ApiCatalogSchemaName);
					}

					break;
				}

				case MongoIndexKind_Hashed:
				{
					if (unique)
					{
						/* This should have been validated but do one more sanity check */
						ereport(ERROR, (errcode(MongoCannotCreateIndex),
										errmsg("Cannot create unique hashed indexes")));
					}

					appendStringInfo(indexExprStr,
									 "%s document %s.%s_rum_hashed_ops(path=%s)",
									 firstColumnWritten ? "," : "",
									 ApiCatalogSchemaName,
									 ExtensionObjectPrefix,
									 quote_literal_cstr(keyPath));
					break;
				}

				case MongoIndexKind_Text:
				{
					if (unique)
					{
						/* This should have been validated but do one more sanity check */
						ereport(ERROR, (errcode(MongoCannotCreateIndex),
										errmsg("Cannot create unique text indexes")));
					}

					appendStringInfo(indexExprStr,
									 "%s document %s.bson_rum_text_path_ops(weights=%s%s%s%s%s%s)",
									 firstColumnWritten ? "," : "",
									 ApiCatalogSchemaName,
									 quote_literal_cstr(SerializeWeightedPaths(
															indexDefKey->textPathList)),
									 indexKeyPath->isWildcard ? ", iswildcard=true" : "",
									 languageOptionKey, languageOptionValue,
									 languageOverrideKey, languageOverrideValue);
					break;
				}

				default:
				{
					ereport(ERROR, (errmsg("Unknown mongo index kind %d",
										   indexKeyPath->indexKind)));
					break;
				}
			}

			/* From Ad-hoc tests, Postgres crashes if this expression becomes too long. Based on data, it seems having more than 2000 characters
			 * in the index options etc, seems to crash the entire thing. To protect from backend crashes, enforce a limit here
			 * Crash is on https://github.com/postgres/postgres/blob/master/src/backend/access/index/indexam.c#L556C9-L556C27
			 */
			int32_t addedLength = indexExprStr->len - currentExprLength;
			if (addedLength >= MAX_INDEX_OPTIONS_LENGTH)
			{
				int lengthDelta = addedLength - MAX_INDEX_OPTIONS_LENGTH;
				ereport(ERROR, (errcode(MongoCannotCreateIndex),
								errmsg(
									"The index path or expression is too long. Try a shorter path or reducing paths by %d characters.",
									lengthDelta),
								errhint(
									"The index path or expression is too long. Try a shorter path or reducing paths by %d characters.",
									lengthDelta)));
			}

			firstColumnWritten = true;
		}
	}

	return indexExprStr->data;
}


/*
 * GenerateIndexFilterStr returns filter expression string to be used in
 * WHERE clause when creating the index whose partial filter expression is
 * determined by given (non-NULL) Expr node.
 */
static char *
GenerateIndexFilterStr(uint64 collectionId, Expr *indexDefPartFilterExpr)
{
	return DeparseSimpleExprForDocument(collectionId, indexDefPartFilterExpr);
}


/*
 * DeparseSimpleExprForDocument returns string by deparsing given non-NULL
 * Expr node assuming that variables in given Expr node only refers to
 * document table with given id.
 */
static char *
DeparseSimpleExprForDocument(uint64 collectionId, Expr *expr)
{
	/* assume relation is already locked */
	Oid documentsTableOid = GetRelationIdForCollectionId(collectionId, NoLock);

	/* check unexpected cases to be on the safe side */
	if (!OidIsValid(documentsTableOid))
	{
		ereport(ERROR, (errmsg("document with id " UINT64_FORMAT " does not exist",
							   collectionId)));
	}
	if (expr == NULL)
	{
		ereport(ERROR, (errmsg("Expr node must not be NULL")));
	}

	/*
	 * Flatten the expression. Indeed, we don't have to do that, just to get
	 * a more readable string when using pg_get_indexdef for debugging/testing.
	 */

	/* no bound params */
	PlannerInfo *root = NULL;
	expr = (Expr *) eval_const_expressions(root, (Node *) expr);

	bool isCheckConstraint = false;
	expr = canonicalize_qual((Expr *) expr, isCheckConstraint);

	/*
	 * Reset search_path so that deparse_expression builds fully
	 * qualified names.
	 */
	OverrideSearchPath *overridePath = GetOverrideSearchPath(CurrentMemoryContext);
	overridePath->schemas = NIL;
	overridePath->addCatalog = true;
	PushOverrideSearchPath(overridePath);

	bool useTableNamePrefix = false;
	bool showImplicitCast = false;
	List *deparseContext = deparse_context_for(get_rel_name(documentsTableOid),
											   documentsTableOid);
	char *str = deparse_expression((Node *) expr, deparseContext,
								   useTableNamePrefix, showImplicitCast);

	/* restore search_path */
	PopOverrideSearchPath();

	return str;
}


/*
 * TryDropCollectionIndexes tries dropping given list of collection indexes.
 *
 * XXX: Theoretically, what we're doing in function can still fail as its
 *      name implies. For this reason, we need to have a maintenance daemon
 *      or a pgcron job to perform cleanup for the invalid indexes that we
 *      might fail to handle here. This is because, there is no point of
 *      trying to perform the clean-up stuff done here more than once.
 *
 *      Note that the periodic job described above should call
 *      AcquireAdvisoryExclusiveLockForCreateIndexes() for the collection before
 *      doing invalid index clean-up for it. Otherwise, it might drop the indexes
 *      being created concurrently as they would appear as invalid in the index
 *      metadata until create_indexes() returns.
 */
static void
TryDropCollectionIndexes(uint64 collectionId, List *indexIdList, List *indexIsUniqueList)
{
	Assert(list_length(indexIdList) == list_length(indexIsUniqueList));

	PG_TRY();
	{
		ListCell *indexIdListCell = NULL;
		ListCell *indexIsUniqueListCell = NULL;
		forboth(indexIdListCell, indexIdList, indexIsUniqueListCell, indexIsUniqueList)
		{
			/* we might or might not have created the pg index .. */
			bool missingOk = true;
			bool concurrently = true;
			DropPostgresIndex(collectionId, lfirst_int(indexIdListCell),
							  lfirst_int(indexIsUniqueListCell),
							  concurrently, missingOk);

			DeleteCollectionIndexRecord(collectionId, lfirst_int(indexIdListCell));
		}
	}
	PG_CATCH();
	{
		ereport(DEBUG1, (errmsg("couldn't perform clean-up for some of the "
								"invalid indexes left behind")));

		/*
		 * We don't much expect any error condition to happen here, but
		 * we still need to be defensive against any kind of failures, such
		 * as OOM.
		 *
		 * For this reason, here we swallow any errors that we could get
		 * during cleanup.
		 */
		FlushErrorState();

		/* need to abort the outer transaction to fire abort handler */
		PopAllActiveSnapshots();
		AbortCurrentTransaction();
		StartTransactionCommand();
	}
	PG_END_TRY();
}


/*
 * This method tries to clean up any transient index left behind for "failedIndex"
 * after an unsuccessful reindex operation.
 *
 * REINDEX CONCURRENTLY has below steps run in separate transactions:
 * 1. Add a transient index definition that has same index name suffixed with "ccnew".
 * 2. A first pass builds the new index from original,
 * and "indisready" is set to true to update new index for new updates and inserts.
 * 3. A second pass is performed to add tuples which were added when first pass was executing.
 * 4. New index is marked valid by setting "indisvalid" to true, and index names are renamed.
 * The old index is suffixed "ccold", i.e. documents_1_rum_index -> documents_1_rum_index_ccold.
 * The new index name suffix "ccnew" is removed, i.e  documents_1_rum_index_ccnew -> documents_1_rum_index.
 * 5. Old "ccold" suffixed index is removed.
 *
 * Now we didn't bother finding the invalid indexes first and then dropping them and
 * only try to drop the "ccnew" or "ccold" suffixed indexes because:
 *
 * 1. There can be no case where both "ccold" and "ccnew" suffixed indexes will persist because of
 * Step 4 and that too is run in a transaction.
 * 2. "ccold" suffixed index remains then it means error occured at Step 5 and REINDEX was succefully able to
 * build the new index but failed to drop the old index which should be dropped in this method.
 * 3. "ccnew" suffixed index remains then it means error occured before Step 4 somewhere and REINDEX was not
 * able to build the new index which is invalid and should be dropped
 */
static void
TryDropFailedCollectionIndexesAfterReIndex(uint64 collectionId,
										   IndexDetails *failedIndex)
{
	Assert(failedIndex != NULL);

	/* Try to delete "ccnew" suffixed index */
	bool concurrently = true;
	bool missingOk = true;
	DropPostgresIndexWithSuffix(collectionId, failedIndex, concurrently,
								missingOk, "_ccnew");

	/* Try to delete "ccold" suffixed index */
	DropPostgresIndexWithSuffix(collectionId, failedIndex, concurrently,
								missingOk, "_ccold");
}


/*
 * MakeCreateIndexesMsg returns a bson object that encapsulates given
 * CreateIndexesResult object.
 */
pgbson *
MakeCreateIndexesMsg(CreateIndexesResult *result)
{
	/* Sharded mongo responses are of the form
	 * {
	 *   raw: {
	 *     "shardId": {
	 *       createdCollectionAutomatically: false,
	 *       numIndexesBefore: 3,
	 *       numIndexesAfter: 5,
	 *       ok: 1
	 *     }
	 *   },
	 *   ok: 1
	 * }
	 */

	pgbson_writer outerWriter;
	PgbsonWriterInit(&outerWriter);

	pgbson_writer rawShardResultWriter;
	PgbsonWriterStartDocument(&outerWriter, "raw", strlen("raw"), &rawShardResultWriter);

	pgbson_writer writer;
	PgbsonWriterStartDocument(&rawShardResultWriter, "defaultShard", strlen(
								  "defaultShard"), &writer);

	if (result->ok)
	{
		PgbsonWriterAppendInt32(&writer, "numIndexesBefore", strlen("numIndexesBefore"),
								result->numIndexesBefore);
		PgbsonWriterAppendInt32(&writer, "numIndexesAfter", strlen("numIndexesAfter"),
								result->numIndexesAfter);
		PgbsonWriterAppendBool(&writer, "createdCollectionAutomatically",
							   strlen("createdCollectionAutomatically"),
							   result->createdCollectionAutomatically);
	}

	if (result->note != NULL)
	{
		PgbsonWriterAppendUtf8(&writer, "note", strlen("note"), result->note);
	}

	PgbsonWriterAppendInt32(&writer, "ok", strlen("ok"), result->ok);

	if (!result->ok)
	{
		if (result->errcode == ERRCODE_T_R_DEADLOCK_DETECTED)
		{
			result->errmsg = "deadlock detected. createIndexes() command "
							 "might cause deadlock when there is a "
							 "concurrent operation that require exclusive "
							 "access on the same collection";
		}
		else if (result->errcode == ERRCODE_UNDEFINED_TABLE)
		{
			result->errcode = MongoIndexBuildAborted;
			result->errmsg = COLLIDX_CONCURRENTLY_DROPPED_RECREATED_ERRMSG;
		}

		PgbsonWriterAppendUtf8(&writer, "errmsg", strlen("errmsg"), result->errmsg);
		PgbsonWriterAppendInt32(&writer, "code", strlen("code"), result->errcode);
	}

	PgbsonWriterEndDocument(&rawShardResultWriter, &writer);
	PgbsonWriterEndDocument(&outerWriter, &rawShardResultWriter);

	if (result->request != NULL)
	{
		pgbson_writer indexIdWriter;
		PgbsonWriterStartDocument(&outerWriter, IndexRequestKey, IndexRequestKeyLength,
								  &indexIdWriter);
		char cmdType[2];
		cmdType[0] = result->request->cmdType;
		cmdType[1] = '\0';
		PgbsonWriterAppendUtf8(&indexIdWriter, CmdTypeKey, CmdTypeKeyLength, cmdType);

		pgbson_array_writer arrayWriter;
		PgbsonWriterStartArray(&indexIdWriter, IdsKey, IdsKeyLength, &arrayWriter);
		ListCell *cell;
		foreach(cell, result->request->indexIds)
		{
			int indexId = lfirst_int(cell);
			bson_value_t elementValue = {
				.value_type = BSON_TYPE_INT32,
				.value.v_int32 = indexId
			};
			PgbsonArrayWriterWriteValue(&arrayWriter, &elementValue);
		}
		PgbsonWriterEndArray(&indexIdWriter, &arrayWriter);
		PgbsonWriterEndDocument(&outerWriter, &indexIdWriter);
	}

	PgbsonWriterAppendInt32(&outerWriter, "ok", strlen("ok"), result->ok);
	return PgbsonWriterGetPgbson(&outerWriter);
}


/*
 * MakeReIndexMsg returns a bson object that encapsulates given
 * ReIndexResult object.
 */
static pgbson *
MakeReIndexMsg(ReIndexResult *result)
{
	/* Build the response of the format for successful reindex or
	 * a faulted response with a reason of failure
	 * {
	 *     nIndexesWas: 1,
	 *     nIndexes: 1,
	 *     indexes: [
	 *                  {
	 *                      "v": 2,
	 *                      "key": {"_id": 1}
	 *                      "name": "_id_"
	 *                  }
	 *              ],
	 *     ok: 1
	 * }
	 * or
	 * {
	 *     ok: 0,
	 *     errmsg: "XYZ",
	 *     code: "ABCD"
	 * }
	 */

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	if (result->ok)
	{
		PgbsonWriterAppendInt32(&writer, "nIndexesWas", strlen("nIndexesWas"),
								result->nIndexesWas);
		PgbsonWriterAppendInt32(&writer, "nIndexes", strlen("nIndexes"),
								result->nIndexes);

		/* List of indexes rebuilt */
		pgbson_array_writer indexesWriter;
		PgbsonWriterStartArray(&writer, "indexes", strlen("indexes"), &indexesWriter);

		ListCell *indexDetailCell = NULL;
		foreach(indexDetailCell, result->indexesDetails)
		{
			const IndexDetails *indexDetail = (IndexDetails *) lfirst(indexDetailCell);
			PgbsonArrayWriterWriteDocument(&indexesWriter, IndexSpecAsBson(
											   &(indexDetail->indexSpec)));
		}
		PgbsonWriterEndArray(&writer, &indexesWriter);
	}

	PgbsonWriterAppendInt32(&writer, "ok", strlen("ok"), result->ok);

	if (!result->ok)
	{
		if (result->errcode == ERRCODE_T_R_DEADLOCK_DETECTED)
		{
			result->errmsg = "deadlock detected. reIndex() command "
							 "might cause deadlock when there is a "
							 "concurrent operation that require exclusive "
							 "access on the same collection";
		}
		else if (result->errcode == ERRCODE_UNDEFINED_TABLE)
		{
			result->errcode = MongoIndexBuildAborted;
			result->errmsg = COLLIDX_CONCURRENTLY_DROPPED_RECREATED_ERRMSG;
		}

		PgbsonWriterAppendUtf8(&writer, "errmsg", strlen("errmsg"), result->errmsg);
		PgbsonWriterAppendInt32(&writer, "code", strlen("code"), result->errcode);
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * SendCreateIndexesResultToClientAsBson sends a bson object to the client
 * based on given CreateIndexesResult object via given DestReceiver, using
 * the tuple descriptor that determines the output of create_indexes() udf.
 */
static void
SendCreateIndexesResultToClientAsBson(FunctionCallInfo createIndexesFcinfo,
									  CreateIndexesResult *result,
									  DestReceiver *destReceiver)
{
	Datum values[2] = { 0 };
	bool isNulls[2] = { false, false };

	values[0] = PointerGetDatum(MakeCreateIndexesMsg(result));
	values[1] = BoolGetDatum(result->ok);

	/* fetch TupleDesc for result, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(createIndexesFcinfo, resultTypeId, &resultTupDesc);

	HeapTuple resultTup = heap_form_tuple(resultTupDesc, values, isNulls);

	SendTupleToClient(resultTup, resultTupDesc, destReceiver);
}


/*
 * SendReIndexResultToClientAsBson sends a bson object to the client
 * based on given ReIndexResult object via given DestReceiver, using
 * the tuple descriptor that determines the output of reindex() udf.
 */
static void
SendReIndexResultToClientAsBson(FunctionCallInfo reIndexesFcinfo,
								ReIndexResult *result,
								DestReceiver *destReceiver)
{
	Datum values[2] = { 0 };
	bool isNulls[2] = { false, false };

	values[0] = PointerGetDatum(MakeReIndexMsg(result));
	values[1] = BoolGetDatum(result->ok);

	/* fetch TupleDesc for result, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(reIndexesFcinfo, resultTypeId, &resultTupDesc);

	HeapTuple resultTup = heap_form_tuple(resultTupDesc, values, isNulls);

	SendTupleToClient(resultTup, resultTupDesc, destReceiver);
}


/*
 * SendTupleToClient sends HeapTuple with given TupleDesc to the client via
 * given DestReceiver.
 */
void
SendTupleToClient(HeapTuple tup, TupleDesc tupDesc, DestReceiver *destReceiver)
{
	HeapTupleHeader tupHeader = DatumGetHeapTupleHeader(HeapTupleGetDatum(tup));
	HeapTupleData tupData = {
		.t_len = HeapTupleHeaderGetDatumLength(tupHeader),
		.t_tableOid = InvalidOid,
		.t_data = tupHeader
	};
	ItemPointerSetInvalid(&(tupData.t_self));

	TupOutputState *outputState = begin_tup_output_tupdesc(destReceiver, tupDesc,
														   &TTSOpsHeapTuple);

	bool shouldFree = false;
	TupleTableSlot *tupSlot = ExecStoreHeapTuple(&tupData, outputState->slot, shouldFree);

	outputState->dest->receiveSlot(tupSlot, outputState->dest);

	end_tup_output(outputState);
}


/*
 * generate_create_index_arg is a UDF only used for testing purposes and
 * designed to simplify creating bson "arg" for ApiSchema.create_indexes()
 * UDF. Example usage is as follows:
 *   CALL ApiSchema.create_indexes(
 *     'db', generate_create_index_arg('collection', '{"a.b.$**": 1}'));
 *   CALL ApiSchema.create_indexes(
 *     'db', generate_create_index_arg('collection', '{"x": 1, "y.z": -1}'));
 *
 * Callers are expected to provide collection name, index name and the bson
 * document that defines the index key.
 */
Datum
generate_create_index_arg(PG_FUNCTION_ARGS)
{
	char *collectionName = text_to_cstring(PG_GETARG_TEXT_P(0));
	char *indexName = text_to_cstring(PG_GETARG_TEXT_P(1));
	pgbson *indexKeyDocument = PG_GETARG_PGBSON(2);

	bson_value_t indexNameValue;
	indexNameValue.value_type = BSON_TYPE_UTF8;
	indexNameValue.value.v_utf8.str = indexName;
	indexNameValue.value.v_utf8.len = strlen(indexName);

	pgbson_writer indexDefWriter;
	PgbsonWriterInit(&indexDefWriter);
	PgbsonWriterAppendValue(&indexDefWriter, "name", 4, &indexNameValue);
	PgbsonWriterAppendDocument(&indexDefWriter, "key", 3, indexKeyDocument);

	bson_value_t collectionNameValue;
	collectionNameValue.value_type = BSON_TYPE_UTF8;
	collectionNameValue.value.v_utf8.str = collectionName;
	collectionNameValue.value.v_utf8.len = strlen(collectionName);

	pgbson_writer argWriter;
	PgbsonWriterInit(&argWriter);
	PgbsonWriterAppendValue(&argWriter, "createIndexes", 13, &collectionNameValue);

	pgbson_array_writer indexesArrayWriter;
	PgbsonWriterStartArray(&argWriter, "indexes", 7, &indexesArrayWriter);
	PgbsonArrayWriterWriteDocument(&indexesArrayWriter,
								   PgbsonWriterGetPgbson(&indexDefWriter));
	PgbsonWriterEndArray(&argWriter, &indexesArrayWriter);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&argWriter));
}


/*
 * Validate index name is not invalid. Constraints on index names can be checked here.
 * Today these are determined empirically from JSTests.
 */
static void
ValidateIndexName(const bson_value_t *indexName)
{
	if (indexName->value.v_utf8.len == 0)
	{
		ereport(ERROR, errcode(MongoCannotCreateIndex),
				errmsg("The index name cannot be empty"));
	}

	/* index names with embedded nulls not allowed */
	if (memchr(indexName->value.v_utf8.str, 0, indexName->value.v_utf8.len) != NULL)
	{
		ereport(ERROR, errcode(MongoCannotCreateIndex),
				errmsg("The index name cannot contain embedded nulls"));
	}

	/* This makes a jstest happy - but can't find docs on other illegal characters */
	if (strcmp(indexName->value.v_utf8.str, "*") == 0)
	{
		ereport(ERROR, errcode(MongoBadValue),
				errmsg("The index name '*' is not valid"));
	}
}


/*
 * Function to create a leaf node specific to CreateIndex Wildcard Projection.
 * This is registered as a callback for calls to BuildBsonPathTree so that the nodes
 * are created with the appropriate types.
 */
static BsonLeafPathNode *
CreateIndexesCreateLeafNode(const StringView *fieldPath, const char *relativePath,
							void *state)
{
	CreateIndexesLeafPathNodeData *leafPathNode = palloc0(
		sizeof(CreateIndexesLeafPathNodeData));
	leafPathNode->relativePath = relativePath;
	return &(leafPathNode->leafPathNode);
}


/*
 * Serializes the paths & weights to a format that can be passed down to the CREATE INDEX.
 */
static const char *
SerializeWeightedPaths(List *weightedPaths)
{
	if (weightedPaths == NIL)
	{
		return "";
	}

	pgbson_writer topLevelWriter;
	PgbsonWriterInit(&topLevelWriter);

	pgbson_writer childWriter;
	PgbsonWriterStartDocument(&topLevelWriter, "", 0, &childWriter);

	ListCell *cell = NULL;
	foreach(cell, weightedPaths)
	{
		TextIndexWeights *weights = lfirst(cell);
		PgbsonWriterAppendDouble(&childWriter, weights->path, -1, weights->weight);
	}

	PgbsonWriterEndDocument(&topLevelWriter, &childWriter);

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(PgbsonWriterGetPgbson(&topLevelWriter), &element);
	return BsonValueToJsonForLogging(&element.bsonValue);
}
