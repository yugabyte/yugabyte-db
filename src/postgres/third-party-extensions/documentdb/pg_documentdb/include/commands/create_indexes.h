/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/create_indexes.h
 *
 * Internal implementation of ApiSchema.create_indexes.
 *
 *-------------------------------------------------------------------------
 */
#ifndef CREATE_INDEXES_H
#define CREATE_INDEXES_H

#include <postgres.h>
#include <tcop/dest.h>
#include <tcop/utility.h>

#include "metadata/index.h"
#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "vector/vector_spec.h"

/*
 * Should be used together with MongoIndexBuildAborted errcode.
 */
#define COLLIDX_CONCURRENTLY_DROPPED_RECREATED_ERRMSG \
	"Index build failed :: caused by :: index or " \
	"collection dropped/re-created concurrently"

extern int32 MaxIndexesPerCollection;

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


typedef struct
{
	/* Whether or not the index path has a wildcard */
	bool isWildcard;

	/* Whether or not the index path has a hashed index */
	bool hasHashedIndexes;

	/* Whether or not the index path has a cosmosdb index */
	bool hasCosmosIndexes;

	/* Whether or not the index path has a text index */
	bool hasTextIndexes;

	/* List of text index paths (each entry will be a TextIndexWeights) */
	List *textPathList;

	/* Whether or not the index path has a 2d index */
	bool has2dIndex;

	/* Whether or not the index path has 2dsphere index */
	bool has2dsphereIndex;

	/*
	 * List of IndexDefKeyPath where each path represents a particular
	 * field/path being indexed if it's not a wildcard index. For example,
	 * {"key" : { "a.b": 1, "c.d": 1 } } would yield keyPathList to be
	 * ["a.b", "c.d"].
	 *
	 * That means, those paths wouldn't contain WILDCARD_INDEX_SUFFIX even
	 * when it is a wildcard index. Evenmore, keyPathList would be an empty
	 * list if wildcard index is on whole document, i.e., doesn't have a
	 * prefixing path. If it's a wildcard index with a prefixing path, then
	 * keyPathList would contain a single element since Mongo doesn't allow
	 * compound wildcard indexes.
	 */
	List *keyPathList;

	MongoIndexKind wildcardIndexKind;
} IndexDefKey;


typedef struct
{
	/* represents value of "indexName" field */
	char *name;

	/** options **/

	/* represents value of "v" field */
	int version;

	/* represents sphere index version */
	int sphereIndexVersion;

	/* represents value of "key" field */
	IndexDefKey *key;

	/* represents value of "unique" field */
	BoolIndexOption unique;

	/* represents value of "wildcardProjection" field */
	const BsonIntermediatePathNode *wildcardProjectionTree;

	/* represents value of "partialFilterExpression" field */
	Expr *partialFilterExpr;

	/* represents value of "sparse" field */
	BoolIndexOption sparse;

	/* document expiry field for TTL index. Null is unspecified.*/
	int *expireAfterSeconds;

	/** bson objects to be stored in metadata **/

	/* raw document hold by "key" field */
	pgbson *keyDocument;

	/* raw document hold by "partialFilterExpression" field */
	pgbson *partialFilterExprDocument;

	/*
	 * Normalized document hold by "wildcardProjection" field.
	 *
	 * e.g.: if "wildcardProjection" document given in index spec is
	 * "{"a.b": 0.4, "b": 5, "a": {"x": 1}, "b": 1}",
	 * then (normalized) wildcardProjDocument would be equal to:
	 * "{"a": {"b": true, "x": true}, "b": true, "_id": false}".
	 *
	 * That means;
	 * - every key is a single-field path
	 * - redundant path specifications are ignored
	 * - inclusion of the paths are specified by booleans
	 * - inclusion of "_id" field is always provided (false by default)
	 */
	pgbson *wildcardProjectionDocument;

	/*
	 * Search options pertinent to Cosmos Search index.
	 */
	CosmosSearchOptions *cosmosSearchOptions;

	/* The default language for text indexes */
	char *defaultLanguage;

	/* The term in the document for specifying language overrides */
	char *languageOverride;

	/* Optional weights document */
	pgbson *weightsDocument;

	/* Optional bounds for 2d index, NULLs are unspecified */
	double *maxBound;
	double *minBound;
	int32_t bits;

	/* Ignorable properties for 2dsphere index */
	int32_t *finestIndexedLevel;
	int32_t *coarsestIndexedLevel;

	/* Feature flag to enable large index term. */
	BoolIndexOption enableLargeIndexKeys;
} IndexDef;

/*
 * For Index creation request in background
 */
typedef struct
{
	List *indexIds;
	char cmdType;
} SubmittedIndexRequests;

/*
 * Contains the data used when building the bson object that needs to be
 * sent to the client after a createIndexes() command.
 */
typedef struct
{
	bool ok;
	bool createdCollectionAutomatically;
	int numIndexesBefore;
	int numIndexesAfter;
	char *note;

	/* error reporting; valid only when "ok" is false */
	char *errmsg;
	int errcode;

	/* For Index creation in background */
	SubmittedIndexRequests *request;
} CreateIndexesResult;


/* Represents whole "arg" document passed to dbcommand/createIndexes */
typedef struct
{
	/* represents value of "createIndexes" field */
	char *collectionName;

	/*
	 * Represents value of "indexes" field.
	 * Contains IndexDef objects for each document in "indexes" array.
	 */
	List *indexDefList;

	/* For unknown index options, ignore or throw error */
	bool ignoreUnknownIndexOptions;

	/* CreateIndex using CREATE INDEX (NON-CONCURRENTLY) blocking the write operations*/
	bool blocking;

	/* TODO: other things such as commitQuorum, comment ... */
} CreateIndexesArg;

bool IsCallCreateIndexesStmt(const Node *node);
bool IsCallReIndexStmt(const Node *node);
CreateIndexesArg ParseCreateIndexesArg(Datum dbNameDatum, pgbson *arg);
CreateIndexesResult create_indexes_non_concurrently(Datum dbNameDatum,
													CreateIndexesArg createIndexesArg,
													bool skipCheckCollectionCreate,
													bool uniqueIndexOnly);
CreateIndexesResult create_indexes_concurrently(Datum dbNameDatum,
												CreateIndexesArg createIndexesArg,
												bool uniqueIndexOnly);
void command_create_indexes(const CallStmt *callStmt,
							ProcessUtilityContext context,
							const ParamListInfo params,
							DestReceiver *destReceiver);
void command_reindex(const CallStmt *callStmt,
					 ProcessUtilityContext context,
					 const ParamListInfo params,
					 DestReceiver *destReceiver);
bool IndexBuildIsInProgress(int indexId);
void InitFCInfoForCallStmt(FunctionCallInfo fcinfo, const CallStmt *callStmt,
						   ProcessUtilityContext context,
						   const ParamListInfo params);
void SendTupleToClient(HeapTuple tup, TupleDesc tupDesc,
					   DestReceiver *destReceiver);
List * CheckForConflictsAndPruneExistingIndexes(uint64 collectionId,
												List *indexDefList,
												List **inBuildIndexIds);
char * CreatePostgresIndexCreationCmd(uint64 collectionId, IndexDef *indexDef, int
									  indexId,
									  bool concurrently, bool isTempCollection);
void ExecuteCreatePostgresIndexCmd(char *cmd, bool concurrently, const Oid userOid,
								   bool useSerialExecution);
void UpdateIndexStatsForPostgresIndex(uint64 collectionId, List *indexIdList);
void AcquireAdvisoryExclusiveLockForCreateIndexes(uint64 collectionId);
IndexSpec MakeIndexSpecForIndexDef(IndexDef *indexDef);
pgbson * MakeCreateIndexesMsg(CreateIndexesResult *result);
bool WildcardProjDocsAreEquivalent(const pgbson *leftWPDocument,
								   const pgbson *rightWPDocument);

#endif
