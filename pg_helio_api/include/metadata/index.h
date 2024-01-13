/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/index.h
 *
 * Accessors around mongo_catalog.collection_indexes.
 *
 *-------------------------------------------------------------------------
 */
#ifndef MONGO_INDEXES_H
#define MONGO_INDEXES_H

#include <postgres.h>
#include <utils/array.h>

#include "io/bson_core.h"

#define INVALID_INDEX_ID ((int) 0)

/* arg1: mongo_catalog.collection_indexes.index_id */
#define MONGO_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX "documents_rum_index_"
#define MONGO_DATA_TABLE_INDEX_NAME_FORMAT MONGO_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX "%d"

#define MONGO_DATA_PRIMARY_KEY_FORMAT_PREFIX "collection_pk_"

#define ID_FIELD_KEY "_id"
#define ID_INDEX_NAME "_id_"
#define CREATE_INDEX_COMMAND_TYPE 'C'
#define REINDEX_COMMAND_TYPE 'R'

#define IndexRequestKey "indexRequest"
#define IndexRequestKeyLength 12
#define CmdTypeKey "cmdType"
#define CmdTypeKeyLength 7
#define IdsKey "ids"
#define IdsKeyLength 3

extern int MaxNumActiveUsersIndexBuilds;

typedef enum BoolIndexOption
{
	BoolIndexOption_Undefined = 0,

	BoolIndexOption_False = 1,

	BoolIndexOption_True = 2,
} BoolIndexOption;

/*
 * Representation of index_spec_type.
 *
 * If you decide making any changes to this struct, consider syncing
 * following functions:
 *  - DatumGetIndexSpec()
 *  - IndexSpecGetDatum()
 *  - CopyIndexSpec()
 *  - IndexSpecOptionsAreEquivalent()
 *  - MakeIndexSpecForIndexDef()
 * , and index_spec_type_internal type in helio_api.sql.
 */
typedef struct IndexSpec
{
	/* Mongo index name, cannot be NULL */
	char *indexName;

	/** index options start here **/

	/* "v" number, must be greater than 0 */
	int indexVersion;

	/* "key" document, cannot be NULL */
	pgbson *indexKeyDocument;

	/* "partialFilterExpression" document, allowed to be NULL */
	pgbson *indexPFEDocument;

	/* normalized form of "wildcardProjection" document, allowed to be NULL */
	pgbson *indexWPDocument;

	/* whether it's "sparse" or undefined */
	BoolIndexOption indexSparse;

	/* whether it's "unique" or undefined */
	BoolIndexOption indexUnique;

	/* document expiry time for TTL index, NULL means it's not specified */
	int *indexExpireAfterSeconds;

	/* Options pertaining to cosmosSearch: TODO: Integrate with indexOptions */
	pgbson *cosmosSearchOptions;

	/* General index options - new options should be appended here. */
	pgbson *indexOptions;
} IndexSpec;

/* A set of text indexing weights for text indexes */
typedef struct TextIndexWeights
{
	/* The path in the doc to index */
	const char *path;

	/* The weight associated with the path */
	double weight;
} TextIndexWeights;

/*
 * Metadata about indexes that can be queried from the index metadata API.
 */
typedef struct IndexDetails
{
	/* index id assigned to this Mongo index */
	int indexId;

	/* collection id of a collection to which this Mongo index belongs*/
	int collectionId;

	/* Mongo index spec */
	IndexSpec indexSpec;
} IndexDetails;

/*
 * Index command status in ApiCatalogSchema.Extension_index_queue
 * Since GetRequestFromIndexQueue relies on order of index_cmd_status to avoid the starvation
 * the enum values are kept in such a way that GetRequestFromIndexQueue always picks request in order
 * IndexCmdStatus_Queued first then IndexCmdStatus_Failed (ascending order).
 */
typedef enum IndexCmdStatus
{
	IndexCmdStatus_Unknown = 0,

	IndexCmdStatus_Queued = 1,

	IndexCmdStatus_Inprogress = 2,

	IndexCmdStatus_Failed = 3,

	IndexCmdStatus_Skippable = 4,
} IndexCmdStatus;

/*
 * Index command request. The command can be of CREATE INDEX/DROP INDEX.
 */
typedef struct IndexCmdRequest
{
	/* Postgres command */
	char *cmd;

	/* index id assigned to this Mongo index */
	int indexId;

	/* collection id of a collection to which this Mongo index belongs*/
	uint64 collectionId;

	/* Internal retry attempt count that we maintain for each request*/
	int16 attemptCount;

	/* comment from previous attempt*/
	pgbson *comment;

	/* Denotes the time when request was updated in the extension_index_queue*/
	TimestampTz updateTime;

	IndexCmdStatus status;
} IndexCmdRequest;

/* Return value of GetIndexBuildJobOpId */
typedef struct
{
	/* timestamp for query start time from pg_stat_activity*/
	TimestampTz start_time;

	/* global_pid from pg_stat_activity*/
	int64 global_pid;
} IndexJobOpId;


/* query index metadata */
IndexDetails * FindIndexWithSpecOptions(uint64 collectionId,
										const IndexSpec *targetIndexSpec);
IndexDetails * IndexIdGetIndexDetails(int indexId);
IndexDetails * IndexNameGetIndexDetails(uint64 collectionId, const char *indexName);
List * IndexKeyGetMatchingIndexes(uint64 collectionId,
								  const pgbson *indexKeyDocument);
List * CollectionIdGetIndexes(uint64 collectionId, bool excludeIdIndex,
							  bool enableNestedDistribution);
List * CollectionIdGetIndexNames(uint64 collectionId, bool excludeIdIndex, bool
								 inProgressOnly);
int CollectionIdGetIndexCount(uint64 collectionId);
int CollectionIdsGetIndexCount(ArrayType *collectionIdsArray);


/* modify/write index metadata */
int RecordCollectionIndex(uint64 collectionId, const IndexSpec *indexSpec,
						  bool isKnownValid);
int MarkIndexesAsValid(uint64 collectionId, const List *indexIdList);
void DeleteAllCollectionIndexRecords(uint64 collectionId);
void DeleteCollectionIndexRecord(uint64 collectionId, int indexId);

List * MergeTextIndexWeights(List *textIndexes, const bson_value_t *weights,
							 bool *isWildCard);

/*
 * An equivalency for Index Specifications.
 */
typedef enum IndexOptionsEquivalency
{
	/*
	 * The index specifications are not equivalent
	 * (They are different)
	 */
	IndexOptionsEquivalency_NotEquivalent,

	/*
	 * The index specifications are equivalent from
	 * an options being compatible with each other
	 * and should be treated as indexes that match
	 * each other but are actually different indexes
	 */
	IndexOptionsEquivalency_Equivalent,

	/*
	 * The index specs are actually the same.
	 */
	IndexOptionsEquivalency_Equal,
} IndexOptionsEquivalency;

/* public helpers for IndexSpec */
IndexOptionsEquivalency IndexSpecOptionsAreEquivalent(const IndexSpec *leftIndexSpec,
													  const IndexSpec *rightIndexSpec);
bool IndexSpecTTLOptionsAreSame(const IndexSpec *leftIndexSpec,
								const IndexSpec *rightIndexSpec);
pgbson * IndexSpecAsBson(const IndexSpec *indexSpec);

/* utilities for index_spec_type and IndexSpec objects */
BoolIndexOption BoolDatumGetBoolIndexOption(bool datumIsNull, Datum datum);
IndexSpec * DatumGetIndexSpec(Datum datum);
IndexSpec MakeIndexSpecForBuiltinIdIndex(void);
Datum IndexSpecGetDatum(IndexSpec *indexSpec);
IndexSpec * CopyIndexSpec(const IndexSpec *indexSpec);
void RemoveRequestFromIndexQueue(int indexId, char cmdType);
void MarkIndexRequestStatus(int indexId, char cmdType, IndexCmdStatus status,
							pgbson *comment,
							IndexJobOpId *opId, int16 attemptCount);
IndexCmdStatus GetIndexBuildStatusFromIndexQueue(int indexId);
IndexCmdRequest * GetRequestFromIndexQueue(char cmdType, uint64 collectionId);
uint64 * GetCollectionIdsForIndexBuild(char cmdType, List *excludeCollectionIds);
void AddRequestInIndexQueue(char *createIndexCmd, int indexId, uint64 collectionId, char
							cmd_type);

/* Static utilities */

static inline bool
GetBoolFromBoolIndexOption(BoolIndexOption option)
{
	return option == BoolIndexOption_Undefined ||
		   option == BoolIndexOption_False ? false : true;
}


#endif
