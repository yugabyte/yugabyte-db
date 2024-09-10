/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/metadata/index.c
 *
 * Accessors around ApiCatalogSchemaName.collection_indexes.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#include <catalog/namespace.h>
#include <commands/sequence.h>
#include <executor/spi.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/expandedrecord.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>

#include <miscadmin.h>
#include "api_hooks.h"
#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "commands/create_indexes.h"
#include "metadata/index.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "utils/list_utils.h"
#include "metadata/relation_utils.h"
#include "utils/guc_utils.h"
#include "utils/query_utils.h"
#include "utils/helio_errors.h"
#include "metadata/metadata_guc.h"
#include "utils/version_utils.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static List * WriteIndexKeyForGetIndexes(pgbson_writer *writer, pgbson *keyDocument);
static pgbson * SerializeIndexSpec(const IndexSpec *spec, bool isGetIndexes,
								   const char *namespaceName);
static IndexOptionsEquivalency IndexKeyDocumentEquivalent(pgbson *leftKey,
														  pgbson *rightKey);
static void DeleteCollectionIndexRecordCore(uint64 collectionId, int *indexId);


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(command_record_id_index);
PG_FUNCTION_INFO_V1(index_spec_options_are_equivalent);
PG_FUNCTION_INFO_V1(command_get_next_collection_index_id);
PG_FUNCTION_INFO_V1(index_spec_as_bson);
PG_FUNCTION_INFO_V1(get_index_spec_as_current_op_command);

/*
 * command_record_id_index inserts a record into ApiCatalogSchemaName.collection_indexes
 * for built-in "_id" index on collection with collectionId.
 *
 * Assumes that index is already created, so sets index_is_valid to true.
 */
Datum
command_record_id_index(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("collectionId cannot be NULL")));
	}
	uint64 collectionId = DatumGetUInt64(PG_GETARG_DATUM(0));

	IndexSpec idIndexSpec = MakeIndexSpecForBuiltinIdIndex();
	bool indexIsValid = true;
	RecordCollectionIndex(collectionId, &idIndexSpec, indexIsValid);

	PG_RETURN_VOID();
}


/*
 * index_spec_options_are_equivalent is the SQL interface for
 * IndexSpecOptionsAreEquivalent.
 */
Datum
index_spec_options_are_equivalent(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("index specs cannot be null")));
	}

	IndexOptionsEquivalency equivalence = IndexSpecOptionsAreEquivalent(
		DatumGetIndexSpec(ExpandedRecordGetDatum(
							  PG_GETARG_EXPANDED_RECORD(0))),
		DatumGetIndexSpec(ExpandedRecordGetDatum(
							  PG_GETARG_EXPANDED_RECORD(1))));
	PG_RETURN_BOOL(equivalence != IndexOptionsEquivalency_NotEquivalent);
}


/*
 * Takes an IndexSpec Datum (Composite type) and converts it to a serialized
 * BSON format. Supports two ways of serializing it:
 * 1) GetIndexes - converts it to a format that the wire protocol "list_indexes" command needs
 * 2) CreateIndexes - converts to a format that create_indexes needs (used in shard_collection)
 *  or similar scenarios.
 */
Datum
index_spec_as_bson(PG_FUNCTION_ARGS)
{
	IndexSpec *indexSpec = DatumGetIndexSpec(ExpandedRecordGetDatum(
												 PG_GETARG_EXPANDED_RECORD(0)));
	bool isGetIndexes = PG_GETARG_BOOL(1);

	char *namespaceName = NULL;
	if (!PG_ARGISNULL(2))
	{
		namespaceName = text_to_cstring(PG_GETARG_TEXT_P(2));
	}

	pgbson *result = SerializeIndexSpec(indexSpec, isGetIndexes, namespaceName);
	PG_RETURN_POINTER(result);
}


/*
 * Takes an IndexSpec Datum (Composite type) and converts it to a serialized
 * BSON format. Converts it to a format that the wire protocol "current_op" command needs.
 * The format is
 * command: {
 *       createIndexes: 'mycollection',
 *       indexes: [ { v: 2, unique: true, key: { name: 1 }, name: 'name_1' } ],
 *       '$db': 'mydatabase'
 *     }
 */
Datum
get_index_spec_as_current_op_command(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("db name cannot be NULL")));
	}
	char *databaseName = text_to_cstring(PG_GETARG_TEXT_P(0));

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("collection name cannot be NULL")));
	}
	char *collectionName = text_to_cstring(PG_GETARG_TEXT_P(1));

	if (PG_ARGISNULL(2))
	{
		ereport(ERROR, (errmsg("Index spec cannot be NULL")));
	}
	IndexSpec *indexSpec = DatumGetIndexSpec(ExpandedRecordGetDatum(
												 PG_GETARG_EXPANDED_RECORD(2)));
	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	WriteIndexSpecAsCurrentOpCommand(&finalWriter, databaseName, collectionName,
									 indexSpec);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * Takes an IndexSpec (Composite type) and writes the index spec to the passed writer in the format that the wire protocol "current_op" command needs.
 * The format is
 * command: {
 *       createIndexes: 'mycollection',
 *       indexes: [ { v: 2, unique: true, key: { name: 1 }, name: 'name_1' } ],
 *       '$db': 'mydatabase'
 *     }
 */
void
WriteIndexSpecAsCurrentOpCommand(pgbson_writer *finalWriter, const char *databaseName,
								 const char *collectionName, const IndexSpec *indexSpec)
{
	bool isGetIndexes = true;
	char *namespaceName = NULL;
	pgbson *result = SerializeIndexSpec(indexSpec, isGetIndexes, namespaceName);

	PgbsonWriterAppendUtf8(finalWriter, "createIndexes", 13, collectionName);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(finalWriter, "indexes", 7, &arrayWriter);
	PgbsonArrayWriterWriteDocument(&arrayWriter, result);
	PgbsonWriterEndArray(finalWriter, &arrayWriter);

	PgbsonWriterAppendUtf8(finalWriter, "$db", 3, databaseName);
}


/*
 * FindIndexWithSpecOptions searches for the index having given IndexSpec and
 * returns an IndexDetails object if it matches an index or NULL if there is
 * no such index.
 */
IndexDetails *
FindIndexWithSpecOptions(uint64 collectionId, const IndexSpec *targetIndexSpec)
{
	const char *cmdStr = FormatSqlQuery(
		"SELECT index_id, index_spec, %s.index_build_is_in_progress(index_id)"
		"FROM %s.collection_indexes "
		"WHERE collection_id = $1 AND %s.index_spec_options_are_equivalent(index_spec, $2) AND"
		" (index_is_valid OR %s.index_build_is_in_progress(index_id))",
		ApiInternalSchemaName, ApiCatalogSchemaName,
		ApiInternalSchemaName, ApiInternalSchemaName);

	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];

	argTypes[0] = INT8OID;
	argValues[0] = UInt64GetDatum(collectionId);

	argTypes[1] = IndexSpecTypeId();
	argValues[1] = IndexSpecGetDatum(CopyIndexSpec(targetIndexSpec));

	/* all args are non-null */
	char *argNulls = NULL;

	bool readOnly = true;

	int numValues = 3;
	bool isNull[3];
	Datum results[3];
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(cmdStr, argCount, argTypes,
												  argValues, argNulls, readOnly,
												  SPI_OK_SELECT, results, isNull,
												  numValues);
	if (isNull[0])
	{
		return NULL;
	}

	IndexDetails *indexDetails = palloc0(sizeof(IndexDetails));
	indexDetails->indexId = DatumGetInt32(results[0]);
	indexDetails->indexSpec = *DatumGetIndexSpec(results[1]);
	indexDetails->collectionId = collectionId;
	indexDetails->isIndexBuildInProgress = DatumGetBool(results[2]);

	return indexDetails;
}


/*
 * IndexIdGetIndexDetails searches for the given indexId and
 * returns an IndexDetails object if found or NULL if there is
 * no such index.
 */
IndexDetails *
IndexIdGetIndexDetails(int indexId)
{
	const char *cmdStr =
		FormatSqlQuery(
			"SELECT collection_id, index_spec, %s.index_build_is_in_progress(index_id)"
			" FROM %s.collection_indexes WHERE index_id = $1",
			ApiInternalSchemaName, ApiCatalogSchemaName);

	int argCount = 1;
	Oid argTypes[1];
	Datum argValues[1];

	argTypes[0] = INT4OID;
	argValues[0] = Int32GetDatum(indexId);

	/* all args are non-null */
	char *argNulls = NULL;
	bool readOnly = true;

	int numValues = 3;
	bool isNull[3];
	Datum results[3];
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(cmdStr, argCount, argTypes,
												  argValues, argNulls, readOnly,
												  SPI_OK_SELECT, results, isNull,
												  numValues);
	if (isNull[0] || isNull[1])
	{
		return NULL;
	}

	IndexDetails *indexDetails = palloc0(sizeof(IndexDetails));
	indexDetails->indexId = indexId;
	indexDetails->collectionId = DatumGetInt64(results[0]);
	indexDetails->indexSpec = *DatumGetIndexSpec(results[1]);
	indexDetails->isIndexBuildInProgress = DatumGetBool(results[2]);

	return indexDetails;
}


/*
 * IndexSpecOptionsAreEquivalent returns true given two IndexSpec's are
 * equivalent except their Mongo index names and expireAfterSeconds options.
 *
 * We don't check them because they should anyway be the same for two indexes
 * as long as all the other options are the same.
 * If it's exactly the same it returns IndexOptionsEquivalency_Equal.
 * If they're not exactly the same, but can still be treated as the same index, then returns
 * IndexOptionsEquivalency_Equivalent.
 * otherwise returns IndexOptionsEquivalency_NotEquivalent.
 */
IndexOptionsEquivalency
IndexSpecOptionsAreEquivalent(const IndexSpec *leftIndexSpec,
							  const IndexSpec *rightIndexSpec)
{
	if (leftIndexSpec->indexVersion != rightIndexSpec->indexVersion)
	{
		return IndexOptionsEquivalency_NotEquivalent;
	}

	bool sparseMatches = (leftIndexSpec->indexSparse == BoolIndexOption_True) ==
						 (rightIndexSpec->indexSparse == BoolIndexOption_True);
	if (!sparseMatches)
	{
		return IndexOptionsEquivalency_NotEquivalent;
	}

	bool uniqueMatches = (leftIndexSpec->indexUnique == BoolIndexOption_True) ==
						 (rightIndexSpec->indexUnique == BoolIndexOption_True);
	if (!uniqueMatches)
	{
		return IndexOptionsEquivalency_NotEquivalent;
	}

	IndexOptionsEquivalency equivalency = IndexKeyDocumentEquivalent(
		leftIndexSpec->indexKeyDocument,
		rightIndexSpec->indexKeyDocument);
	if (equivalency == IndexOptionsEquivalency_NotEquivalent || equivalency ==
		IndexOptionsEquivalency_TextEquivalent)
	{
		return equivalency;
	}

	if (leftIndexSpec->indexWPDocument == NULL &&
		rightIndexSpec->indexWPDocument == NULL)
	{
		/* both are NULL, check for other options */
	}
	else if (leftIndexSpec->indexWPDocument == NULL ||
			 rightIndexSpec->indexWPDocument == NULL)
	{
		/* one of them is NULL but not the other */
		return IndexOptionsEquivalency_NotEquivalent;
	}
	else if (!WildcardProjDocsAreEquivalent(leftIndexSpec->indexWPDocument,
											rightIndexSpec->indexWPDocument))
	{
		return IndexOptionsEquivalency_NotEquivalent;
	}

	if (leftIndexSpec->indexPFEDocument == NULL &&
		rightIndexSpec->indexPFEDocument == NULL)
	{
		/* both are NULL, check for other options */
	}
	else if (leftIndexSpec->indexPFEDocument == NULL ||
			 rightIndexSpec->indexPFEDocument == NULL)
	{
		/* one of them is NULL but not the other */
		return IndexOptionsEquivalency_NotEquivalent;
	}
	else if (!QueryDocumentsAreEquivalent(leftIndexSpec->indexPFEDocument,
										  rightIndexSpec->indexPFEDocument))
	{
		return IndexOptionsEquivalency_NotEquivalent;
	}

	if (leftIndexSpec->cosmosSearchOptions == NULL &&
		rightIndexSpec->cosmosSearchOptions == NULL)
	{
		/* both are NULL, check for other options */
	}
	else if (leftIndexSpec->cosmosSearchOptions == NULL ||
			 rightIndexSpec->cosmosSearchOptions == NULL)
	{
		return IndexOptionsEquivalency_NotEquivalent;
	}
	else if (!PgbsonEquals(leftIndexSpec->cosmosSearchOptions,
						   rightIndexSpec->cosmosSearchOptions))
	{
		/* Same index name, but different cosmos search options */
		if (strcmp(leftIndexSpec->indexName, rightIndexSpec->indexName) == 0)
		{
			return IndexOptionsEquivalency_NotEquivalent;
		}
		else
		{
			/* Different index name, different cosmos search options, but on same path */
			if (PgbsonEquals(leftIndexSpec->indexKeyDocument,
							 rightIndexSpec->indexKeyDocument))
			{
				return IndexOptionsEquivalency_Equivalent;
			}
		}
		return IndexOptionsEquivalency_NotEquivalent;
	}

	if (leftIndexSpec->indexOptions == NULL &&
		rightIndexSpec->indexOptions == NULL)
	{
		/* both are NULL, check for other options */
	}
	else if (leftIndexSpec->indexOptions == NULL ||
			 rightIndexSpec->indexOptions == NULL)
	{
		/* TODO: update this as more options get thrown into indexOptions */
		return equivalency == IndexOptionsEquivalency_Equal ?
			   IndexOptionsEquivalency_Equivalent :
			   equivalency;
	}
	else if (!PgbsonEquals(leftIndexSpec->indexOptions,
						   rightIndexSpec->indexOptions))
	{
		/* TODO: update this as more options get thrown into indexOptions */
		return equivalency == IndexOptionsEquivalency_Equal ?
			   IndexOptionsEquivalency_Equivalent :
			   equivalency;
	}

	return equivalency;
}


/*
 * IndexSpecTTLOptionsAreSame returns true if given two IndexSpec's
 * indexExpireAfterSeconds fields are equal.
 */
bool
IndexSpecTTLOptionsAreSame(const IndexSpec *leftIndexSpec,
						   const IndexSpec *rightIndexSpec)
{
	if (leftIndexSpec->indexExpireAfterSeconds == NULL &&
		rightIndexSpec->indexExpireAfterSeconds == NULL)
	{
		return true;
	}
	else if (leftIndexSpec->indexExpireAfterSeconds == NULL ||
			 rightIndexSpec->indexExpireAfterSeconds == NULL)
	{
		/* one of them is NULL but not the other */
		return false;
	}
	else
	{
		/* both are non-NULL */
		return *leftIndexSpec->indexExpireAfterSeconds ==
			   *rightIndexSpec->indexExpireAfterSeconds;
	}
}


/*
 * RecordCollectionIndex inserts a record into ApiCatalogSchemaName.collection_indexes
 * for given index using SPI and returns the index_id assigned to that index.
 */
int
RecordCollectionIndex(uint64 collectionId, const IndexSpec *indexSpec,
					  bool isKnownValid)
{
	const char *cmdStr = FormatSqlQuery("INSERT INTO %s.collection_indexes "
										"(collection_id, index_spec, index_is_valid) "
										"VALUES ($1, $2, $3) RETURNING index_id",
										ApiCatalogSchemaName);

	int argCount = 3;
	Oid argTypes[3];
	Datum argValues[3];
	char argNulls[3] = { ' ', ' ', ' ' };

	argTypes[0] = INT8OID;
	argValues[0] = UInt64GetDatum(collectionId);

	argTypes[1] = IndexSpecTypeId();
	argValues[1] = IndexSpecGetDatum(CopyIndexSpec(indexSpec));

	argTypes[2] = BOOLOID;
	argValues[2] = BoolGetDatum(isKnownValid);

	bool isNull = true;
	Datum resultDatum = RunQueryWithCommutativeWrites(cmdStr, argCount, argTypes,
													  argValues, argNulls,
													  SPI_OK_INSERT_RETURNING,
													  &isNull);

	if (isNull)
	{
		/* not expecting this to happen but .. */
		ereport(ERROR, (errmsg("unexpected error when inserting record into "
							   "index metadata")));
	}

	return DatumGetInt32(resultDatum);
}


/*
 * MarkIndexesAsValid takes a list of index ids and marks the index metadata
 * entries associated with them as valid (if inserted already).
 *
 * Returns number of index metadata entries processed.
 */
int
MarkIndexesAsValid(uint64 collectionId, const List *indexIdList)
{
	const char *cmdStr =
		FormatSqlQuery("WITH cte AS ( UPDATE %s.collection_indexes"
					   " SET index_is_valid = true WHERE collection_id = $1"
					   "  AND index_id = ANY($2) RETURNING 1) SELECT COUNT(*) FROM cte",
					   ApiCatalogSchemaName);

	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];
	char argNulls[2] = { ' ', ' ' };

	argTypes[0] = INT8OID;
	argValues[0] = UInt64GetDatum(collectionId);

	argTypes[1] = INT4ARRAYOID;
	argValues[1] = PointerGetDatum(IntListGetPgIntArray(indexIdList));

	bool isNull = true;
	Datum resultDatum = RunQueryWithCommutativeWrites(cmdStr, argCount, argTypes,
													  argValues, argNulls,
													  SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		ereport(ERROR, (errmsg("unexpected error when updating index metadata "
							   "records")));
	}

	if (DatumGetInt64(resultDatum) > INT_MAX)
	{
		ereport(ERROR, (errmsg("found too many indexes in index metadata")));
	}

	return DatumGetInt64(resultDatum);
}


/*
 * CollectionIdGetIndexNames returns names of the indexes that collection
 * with collectionId has. If 'inProgressOnly' flag is true, it returns
 * only the index names whose build is in progress.
 */
List *
CollectionIdGetIndexNames(uint64 collectionId, bool excludeIdIndex, bool inProgressOnly)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT array_agg((index_spec).index_name ORDER BY index_id) "
					 "FROM %s.collection_indexes WHERE collection_id = " UINT64_FORMAT,
					 ApiCatalogSchemaName, collectionId);

	if (inProgressOnly)
	{
		appendStringInfo(cmdStr,
						 " AND %s.index_build_is_in_progress(index_id)",
						 ApiInternalSchemaName);
	}
	else
	{
		appendStringInfo(cmdStr,
						 " AND (index_is_valid OR %s.index_build_is_in_progress(index_id))",
						 ApiInternalSchemaName);
	}

	if (excludeIdIndex)
	{
		appendStringInfo(cmdStr, " AND (index_spec).index_name != %s",
						 quote_literal_cstr(ID_INDEX_NAME));
	}

	bool isNull = true;
	bool readOnly = true;
	Datum resultDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
													SPI_OK_SELECT, &isNull);
	if (isNull)
	{
		/* collection has no indexes */
		return NIL;
	}

	ArrayType *array = DatumGetArrayTypeP(resultDatum);

	/* error if any Datums are NULL */
	bool **nulls = NULL;

	Datum *elems = NULL;
	int nelems = 0;
	ArrayExtractDatums(array, TEXTOID, &elems, nulls, &nelems);

	List *indexNameList = NIL;

	for (int i = 0; i < nelems; i++)
	{
		indexNameList = lappend(indexNameList, TextDatumGetCString(elems[i]));
	}

	return indexNameList;
}


static List *
CollectionIdGetIndexesCore(uint64 collectionId, bool excludeIdIndex,
						   bool enableNestedDistribution, bool includeIndexBuilds)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT array_agg(a.index_id), array_agg(a.index_spec), "
					 " array_agg(a.index_build_in_progress) FROM (");

	appendStringInfo(cmdStr, " SELECT index_id, index_spec");
	if (includeIndexBuilds)
	{
		appendStringInfo(cmdStr,
						 ", %s.index_build_is_in_progress(index_id) AS index_build_in_progress",
						 ApiInternalSchemaName);
	}
	else
	{
		appendStringInfo(cmdStr, ", FALSE AS index_build_in_progress");
	}

	appendStringInfo(cmdStr,
					 " FROM %s.collection_indexes WHERE collection_id = " UINT64_FORMAT,
					 ApiCatalogSchemaName, collectionId);

	if (includeIndexBuilds)
	{
		appendStringInfo(cmdStr,
						 " AND (index_is_valid OR %s.index_build_is_in_progress(index_id))",
						 ApiInternalSchemaName);
	}
	else
	{
		appendStringInfo(cmdStr, " AND index_is_valid");
	}

	if (excludeIdIndex)
	{
		appendStringInfo(cmdStr, " AND (index_spec).index_name != %s",
						 quote_literal_cstr(ID_INDEX_NAME));
	}

	/* Closed the inner query */
	appendStringInfo(cmdStr, " ORDER BY index_id) a");

	bool readOnly = true;
	int numValues = 3;
	bool isNull[3];
	Datum results[3];
	if (enableNestedDistribution)
	{
		int nArgs = 0;
		Oid *argTypes = NULL;
		Datum *argValues = NULL;
		char *argNulls = NULL;
		RunMultiValueQueryWithNestedDistribution(cmdStr->data, nArgs, argTypes, argValues,
												 argNulls,
												 readOnly, SPI_OK_SELECT, results, isNull,
												 numValues);
	}
	else
	{
		ExtensionExecuteMultiValueQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
											  results,
											  isNull, numValues);
	}

	if (isNull[0])
	{
		/* collection has no indexes */
		return NIL;
	}

	ArrayType *array = DatumGetArrayTypeP(results[1]);

	/* error if any Datums are NULL */
	bool **nulls = NULL;

	Datum *elems = NULL;
	int nelems = 0;
	ArrayExtractDatums(array, IndexSpecTypeId(), &elems, nulls, &nelems);

	ArrayType *idArray = DatumGetArrayTypeP(results[0]);
	Datum *idElems = NULL;
	int nIdelems = 0;
	ArrayExtractDatums(idArray, INT4OID, &idElems, nulls, &nIdelems);

	ArrayType *indexBuildInProgressArray = DatumGetArrayTypeP(results[2]);
	Datum *buildProgressElems = NULL;
	int nProgressElems = 0;
	ArrayExtractDatums(indexBuildInProgressArray, BOOLOID, &buildProgressElems, nulls,
					   &nProgressElems);

	List *indexesList = NIL;

	for (int i = 0; i < nelems; i++)
	{
		/* Both arrays should have one to one mapping of index spec and ids */
		IndexDetails *indexDetail = palloc(sizeof(IndexDetails));
		indexDetail->indexId = DatumGetInt64(idElems[i]);
		indexDetail->indexSpec = *DatumGetIndexSpec(elems[i]);
		indexDetail->collectionId = collectionId;
		indexDetail->isIndexBuildInProgress = DatumGetBool(buildProgressElems[i]);
		indexesList = lappend(indexesList, (void *) indexDetail);
	}

	return indexesList;
}


/*
 * CollectionIdGetIndexes returns List of IndexDetails of the indexes that collection
 * with `collectionId` has.
 *
 * Parameters-
 * uint64 collectionId: collectionId to get indexes for
 * bool excludeIdIndex: Whether or not to include _id default index in the result
 * bool enableNestedDistribution: Whether or not to allow nested distribution for the query:
 *  Note: Only to be used in non-data critical paths.
 */
List *
CollectionIdGetIndexes(uint64 collectionId, bool excludeIdIndex,
					   bool enableNestedDistribution)
{
	bool includeIndexBuilds = true;
	return CollectionIdGetIndexesCore(collectionId, excludeIdIndex,
									  enableNestedDistribution, includeIndexBuilds);
}


/*
 * Like CollectionIdGetIndexes but does not consult index build in progress.
 */
List *
CollectionIdGetValidIndexes(uint64 collectionId, bool excludeIdIndex,
							bool enableNestedDistribution)
{
	bool includeIndexBuilds = false;
	return CollectionIdGetIndexesCore(collectionId, excludeIdIndex,
									  enableNestedDistribution, includeIndexBuilds);
}


/*
 * CollectionIdGetIndexCount returns number of indexes that collection with
 * collectionId has.
 */
int
CollectionIdGetIndexCount(uint64 collectionId)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT COUNT(*) "
					 "FROM %s.collection_indexes "
					 "WHERE collection_id = " UINT64_FORMAT
					 " AND (index_is_valid OR %s.index_build_is_in_progress(index_id))",
					 ApiCatalogSchemaName, collectionId, ApiInternalSchemaName);

	bool isNull = true;
	bool readOnly = true;
	Datum resultDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
													SPI_OK_SELECT, &isNull);
	Assert(!isNull);

	if (DatumGetInt64(resultDatum) > INT_MAX)
	{
		ereport(ERROR, (errmsg("found too many indexes in index metadata")));
	}

	return DatumGetInt64(resultDatum);
}


/*
 * CollectionIdsGetIndexCount returns number of valid indexes that all the collections
 * in the given array collections has.
 * This function should be called at coordinator only as the index metadata table
 * is available on coordinator.
 */
int
CollectionIdsGetIndexCount(ArrayType *collectionIdsArray)
{
	const char *query =
		FormatSqlQuery("SELECT COUNT(*) FROM %s.collection_indexes "
					   "WHERE collection_id = ANY($1) AND (index_is_valid OR %s.index_build_is_in_progress(index_id))",
					   ApiCatalogSchemaName, ApiInternalSchemaName);

	int nargs = 1;
	Oid argTypes[1] = { INT8ARRAYOID };
	Datum argValues[1] = { PointerGetDatum(collectionIdsArray) };

	bool isReadOnly = true;
	bool isNull = false;
	Datum resultDatum = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes,
															argValues,
															NULL, isReadOnly,
															SPI_OK_SELECT,
															&isNull);


	Assert(!isNull);
	if (DatumGetInt64(resultDatum) > INT_MAX)
	{
		ereport(ERROR, (errmsg("found too many indexes in index metadata")));
	}

	return DatumGetInt64(resultDatum);
}


/*
 * DeleteAllCollectionIndexRecords wrapper for DeleteCollectionIndexRecordCore that deletes
 * all indexes of a collection. The delete is done as a commutative write
 */
void
DeleteAllCollectionIndexRecords(uint64 collectionId)
{
	DeleteCollectionIndexRecordCore(collectionId, NULL);
}


/*
 * DeleteCollectionIndexRecord wrapper for DeleteCollectionIndexRecordCore that deletes with a
 * specific indexId. The delete is done as a commutative write
 */
void
DeleteCollectionIndexRecord(uint64 collectionId, int indexId)
{
	DeleteCollectionIndexRecordCore(collectionId, &indexId);
}


/*
 * DeleteCollectionIndexRecordCore deletes the record inserted for given index from
 * mongo_catalog.collection_indexes using SPI. Delete all indexes of the collection
 * if indexId is NULL. The delete is done as a commutative write
 */
static void
DeleteCollectionIndexRecordCore(uint64 collectionId, int *indexId)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "DELETE FROM %s.collection_indexes WHERE "
					 "collection_id = " UINT64_FORMAT,
					 ApiCatalogSchemaName, collectionId);

	if (indexId != NULL)
	{
		appendStringInfo(cmdStr,
						 " AND index_id = %d", *indexId);
	}

	bool isNull = true;
	int nargs = 0;
	Oid *argTypes = NULL;
	Datum *argValues = NULL;
	char *argNulls = NULL;
	RunQueryWithCommutativeWrites(cmdStr->data, nargs, argTypes, argValues, argNulls,
								  SPI_OK_DELETE, &isNull);

	Assert(isNull);
}


/*
 * IndexNameGetIndexDetails returns IndexDetails for the index with given
 * "name" using SPI. Returns NULL If no such index exists.
 */
IndexDetails *
IndexNameGetIndexDetails(uint64 collectionId, const char *indexName)
{
	const char *cmdStr = FormatSqlQuery(
		"SELECT index_id, index_spec, %s.index_build_is_in_progress(index_id) "
		"FROM %s.collection_indexes WHERE collection_id = $1 AND"
		" (index_spec).index_name = $2 AND"
		" (index_is_valid OR %s.index_build_is_in_progress(index_id))",
		ApiInternalSchemaName, ApiCatalogSchemaName,
		ApiInternalSchemaName);

	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];

	argTypes[0] = INT8OID;
	argValues[0] = UInt64GetDatum(collectionId);

	argTypes[1] = TEXTOID;
	argValues[1] = CStringGetTextDatum(indexName);

	/* all args are non-null */
	char *argNulls = NULL;

	bool readOnly = true;

	int numValues = 3;
	bool isNull[3];
	Datum results[3];
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(cmdStr, argCount, argTypes,
												  argValues, argNulls, readOnly,
												  SPI_OK_SELECT, results, isNull,
												  numValues);
	if (isNull[0])
	{
		return NULL;
	}

	IndexDetails *indexDetails = palloc0(sizeof(IndexDetails));
	indexDetails->indexId = DatumGetInt32(results[0]);
	indexDetails->indexSpec = *DatumGetIndexSpec(results[1]);
	indexDetails->collectionId = collectionId;
	indexDetails->isIndexBuildInProgress = DatumGetBool(results[2]);

	return indexDetails;
}


/*
 * IndexKeyGetMatchingIndexes returns a list of IndexDetails objects for the
 * valid indexes with given "key" using SPI.
 *
 * Returns indexes in the order of their ids.
 */
List *
IndexKeyGetMatchingIndexes(uint64 collectionId, const pgbson *indexKeyDocument)
{
	const char *cmdStr =
		FormatSqlQuery(
			"SELECT array_agg(index_id ORDER BY index_id), array_agg(index_spec ORDER BY index_id), "
			" array_agg(%s.index_build_is_in_progress(index_id) ORDER BY index_id) "
			"FROM %s.collection_indexes WHERE collection_id = $1 AND"
			" (index_spec).index_key::%s OPERATOR(%s.=) $2::%s AND"
			" (index_is_valid OR %s.index_build_is_in_progress(index_id))",
			ApiInternalSchemaName, ApiCatalogSchemaName, FullBsonTypeName, CoreSchemaName,
			FullBsonTypeName, ApiInternalSchemaName);

	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];

	argTypes[0] = INT8OID;
	argValues[0] = UInt64GetDatum(collectionId);

	argTypes[1] = BsonTypeId();
	argValues[1] = PointerGetDatum(indexKeyDocument);

	/* all args are non-null */
	char *argNulls = NULL;

	bool readOnly = true;

	int numValues = 3;
	bool isNull[3];
	Datum results[3];
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(cmdStr, argCount, argTypes,
												  argValues, argNulls, readOnly,
												  SPI_OK_SELECT, results, isNull,
												  numValues);

	if (isNull[0])
	{
		return NIL;
	}

	ArrayType *indexIdArray = DatumGetArrayTypeP(results[0]);
	ArrayType *indexSpecArray = DatumGetArrayTypeP(results[1]);
	ArrayType *indexBuildInProgressArray = DatumGetArrayTypeP(results[2]);

	/* error if any Datums are NULL */
	bool **nulls = NULL;

	Datum *indexIdArrayElems = NULL;
	int nIndexIdArrayElems = 0;
	ArrayExtractDatums(indexIdArray, INT4OID, &indexIdArrayElems, nulls,
					   &nIndexIdArrayElems);

	Datum *indexSpecArrayElems = NULL;
	int nIndexSpecArrayElems = 0;
	ArrayExtractDatums(indexSpecArray, IndexSpecTypeId(), &indexSpecArrayElems, nulls,
					   &nIndexSpecArrayElems);

	Assert(nIndexIdArrayElems == nIndexSpecArrayElems);

	Datum *indexBuildProgressArrayElems = NULL;
	int nIndexIsValidArrayElems = 0;
	ArrayExtractDatums(indexBuildInProgressArray, BOOLOID, &indexBuildProgressArrayElems,
					   nulls,
					   &nIndexIsValidArrayElems);
	Assert(nIndexIsValidArrayElems == nIndexIdArrayElems);

	List *keyMatchedIndexDetailsList = NIL;
	for (int i = 0; i < nIndexIdArrayElems; i++)
	{
		IndexDetails *indexDetails = palloc0(sizeof(IndexDetails));
		indexDetails->indexId = DatumGetInt32(indexIdArrayElems[i]);
		indexDetails->indexSpec = *DatumGetIndexSpec(indexSpecArrayElems[i]);
		indexDetails->collectionId = collectionId;
		indexDetails->isIndexBuildInProgress = indexBuildProgressArrayElems[i];

		keyMatchedIndexDetailsList = lappend(keyMatchedIndexDetailsList, indexDetails);
	}

	return keyMatchedIndexDetailsList;
}


/*
 * DatumGetIndexSpec converts given Datum of type index_spec_type into an
 * IndexSpec object.
 */
IndexSpec *
DatumGetIndexSpec(Datum indexSpecDatum)
{
	IndexSpec *indexSpec = palloc0(sizeof(IndexSpec));

	ExpandedRecordHeader *header = DatumGetExpandedRecord(indexSpecDatum);

	bool isNull = false;
	Datum datum = 0;

	datum = expanded_record_fetch_field(header, 1, &isNull);
	Assert(!isNull);
	indexSpec->indexName = TextDatumGetCString(datum);

	datum = expanded_record_fetch_field(header, 2, &isNull);
	Assert(!isNull);
	indexSpec->indexKeyDocument = DatumGetPgBson(datum);

	datum = expanded_record_fetch_field(header, 3, &isNull);
	indexSpec->indexPFEDocument = isNull ? NULL : DatumGetPgBson(datum);

	datum = expanded_record_fetch_field(header, 4, &isNull);
	indexSpec->indexWPDocument = isNull ? NULL : DatumGetPgBson(datum);

	datum = expanded_record_fetch_field(header, 5, &isNull);
	indexSpec->indexSparse = BoolDatumGetBoolIndexOption(isNull, datum);

	datum = expanded_record_fetch_field(header, 6, &isNull);
	indexSpec->indexUnique = BoolDatumGetBoolIndexOption(isNull, datum);

	datum = expanded_record_fetch_field(header, 7, &isNull);
	Assert(!isNull);
	indexSpec->indexVersion = DatumGetInt32(datum);

	datum = expanded_record_fetch_field(header, 8, &isNull);
	if (!isNull)
	{
		indexSpec->indexExpireAfterSeconds = palloc0(sizeof(int));
		*indexSpec->indexExpireAfterSeconds = DatumGetInt32(datum);
	}

	datum = expanded_record_fetch_field(header, 9, &isNull);
	indexSpec->cosmosSearchOptions = NULL;
	if (!isNull)
	{
		indexSpec->cosmosSearchOptions = DatumGetPgBson(datum);
	}

	/* For the case where new binary is in, but Upgrade hasn't run */
	indexSpec->indexOptions = NULL;
	if (header->nfields > 9)
	{
		datum = expanded_record_fetch_field(header, 10, &isNull);
		if (!isNull)
		{
			indexSpec->indexOptions = DatumGetPgBson(datum);
		}
	}

	return indexSpec;
}


/*
 * BoolDatumGetBoolIndexOption returns BoolIndexOption for given bool Datum.
 */
BoolIndexOption
BoolDatumGetBoolIndexOption(bool datumIsNull, Datum datum)
{
	if (datumIsNull)
	{
		return BoolIndexOption_Undefined;
	}
	else if (DatumGetBool(datum))
	{
		return BoolIndexOption_True;
	}
	else
	{
		return BoolIndexOption_False;
	}
}


/*
 * IndexSpecAsBson is the C interface for ApiInternalSchema.index_spec_as_bson.
 */
pgbson *
IndexSpecAsBson(const IndexSpec *indexSpec)
{
	bool isGetIndexes = false;
	const char *namespaceName = NULL;
	return SerializeIndexSpec(indexSpec, isGetIndexes, namespaceName);
}


/*
 * MakeIndexSpecForBuiltinIdIndex returns an IndexSpec for a built-in _id index.
 */
IndexSpec
MakeIndexSpecForBuiltinIdIndex(void)
{
	pgbson_writer indexKeyDocumentWriter;
	PgbsonWriterInit(&indexKeyDocumentWriter);
	int idIndexOrdering = 1;
	PgbsonWriterAppendInt32(&indexKeyDocumentWriter, ID_FIELD_KEY,
							strlen(ID_FIELD_KEY), idIndexOrdering);

	IndexSpec idIndexSpec = {
		.indexName = ID_INDEX_NAME,
		.indexVersion = 2,
		.indexKeyDocument = PgbsonWriterGetPgbson(&indexKeyDocumentWriter),
		.indexPFEDocument = NULL,
		.indexWPDocument = NULL,
		.indexSparse = BoolIndexOption_Undefined,

		/*
		 * Even though the _id index is unique, Mongo reports it as
		 * "non unique".
		 */
		.indexUnique = BoolIndexOption_Undefined,
		.indexExpireAfterSeconds = NULL,
		.cosmosSearchOptions = NULL,
		.indexOptions = NULL
	};

	return idIndexSpec;
}


/*
 * IndexSpecGetDatum converts given IndexSpec into a Datum of type
 * index_spec_type.
 */
Datum
IndexSpecGetDatum(IndexSpec *indexSpec)
{
	Datum argValues[10];
	bool argNulls[10];

	argValues[0] = CStringGetTextDatum(indexSpec->indexName);
	argNulls[0] = false;

	argValues[1] = PointerGetDatum(indexSpec->indexKeyDocument);
	argNulls[1] = false;

	if (indexSpec->indexPFEDocument)
	{
		argValues[2] = PointerGetDatum(indexSpec->indexPFEDocument);
		argNulls[2] = false;
	}
	else
	{
		argNulls[2] = true;
	}

	if (indexSpec->indexWPDocument)
	{
		argValues[3] = PointerGetDatum(indexSpec->indexWPDocument);
		argNulls[3] = false;
	}
	else
	{
		argNulls[3] = true;
	}

	if (indexSpec->indexSparse != BoolIndexOption_Undefined)
	{
		argValues[4] = BoolGetDatum(indexSpec->indexSparse == BoolIndexOption_True);
		argNulls[4] = false;
	}
	else
	{
		argNulls[4] = true;
	}

	if (indexSpec->indexUnique != BoolIndexOption_Undefined)
	{
		argValues[5] = BoolGetDatum(indexSpec->indexUnique == BoolIndexOption_True);
		argNulls[5] = false;
	}
	else
	{
		argNulls[5] = true;
	}

	argValues[6] = Int32GetDatum(indexSpec->indexVersion);
	argNulls[6] = false;

	if (indexSpec->indexExpireAfterSeconds != NULL)
	{
		argValues[7] = Int32GetDatum(*indexSpec->indexExpireAfterSeconds);
		argNulls[7] = false;
	}
	else
	{
		argNulls[7] = true;
	}

	if (indexSpec->cosmosSearchOptions != NULL)
	{
		argValues[8] = PointerGetDatum(indexSpec->cosmosSearchOptions);
		argNulls[8] = false;
	}
	else
	{
		argNulls[8] = true;
	}

	if (indexSpec->indexOptions != NULL)
	{
		argValues[9] = PointerGetDatum(indexSpec->indexOptions);
		argNulls[9] = false;
	}
	else
	{
		argNulls[9] = true;
	}

	ExpandedRecordHeader *header = make_expanded_record_from_typeid(
		IndexSpecTypeId(), -1, CurrentMemoryContext);
	expanded_record_set_fields(header, argValues, argNulls, false);

	return ExpandedRecordGetDatum(header);
}


/*
 * CopyIndexSpec returns a copy of given IndexSpec.
 */
IndexSpec *
CopyIndexSpec(const IndexSpec *indexSpec)
{
	IndexSpec *copiedIndexSpec = palloc0(sizeof(IndexSpec));

	copiedIndexSpec->indexName = pstrdup(indexSpec->indexName);
	copiedIndexSpec->indexKeyDocument =
		CopyPgbsonIntoMemoryContext(indexSpec->indexKeyDocument, CurrentMemoryContext);

	if (indexSpec->indexPFEDocument)
	{
		copiedIndexSpec->indexPFEDocument =
			CopyPgbsonIntoMemoryContext(indexSpec->indexPFEDocument,
										CurrentMemoryContext);
	}

	if (indexSpec->indexWPDocument)
	{
		copiedIndexSpec->indexWPDocument =
			CopyPgbsonIntoMemoryContext(indexSpec->indexWPDocument,
										CurrentMemoryContext);
	}

	copiedIndexSpec->indexSparse = indexSpec->indexSparse;
	copiedIndexSpec->indexUnique = indexSpec->indexUnique;
	copiedIndexSpec->indexVersion = indexSpec->indexVersion;

	if (indexSpec->indexExpireAfterSeconds)
	{
		copiedIndexSpec->indexExpireAfterSeconds = palloc0(sizeof(int));
		*copiedIndexSpec->indexExpireAfterSeconds = *indexSpec->indexExpireAfterSeconds;
	}

	if (indexSpec->cosmosSearchOptions)
	{
		copiedIndexSpec->cosmosSearchOptions = CopyPgbsonIntoMemoryContext(
			indexSpec->cosmosSearchOptions,
			CurrentMemoryContext);
	}

	if (indexSpec->indexOptions)
	{
		copiedIndexSpec->indexOptions = CopyPgbsonIntoMemoryContext(
			indexSpec->indexOptions,
			CurrentMemoryContext);
	}

	return copiedIndexSpec;
}


/*
 * command_get_next_collection_index_id returns next unique collection index
 * id based on the value of helio_api.next_collection_index_id GUC if it is set.
 *
 * Otherwise, uses the next value of collection_indexes_index_id_seq sequence.
 *
 * Note that helio_api.next_collection_index_id GUC is only expected to be set
 * in regression tests to ensure consistent collection index ids when running
 * tests in parallel.
 */
Datum
command_get_next_collection_index_id(PG_FUNCTION_ARGS)
{
	if (NextCollectionIndexId != NEXT_COLLECTION_INDEX_ID_UNSET)
	{
		int collectionIndexId = NextCollectionIndexId++;
		PG_RETURN_DATUM(Int32GetDatum(collectionIndexId));
	}

	PG_RETURN_INT32(DatumGetInt32(
						SequenceGetNextValAsUser(ApiCatalogCollectionIndexIdSequenceId(),
												 HelioApiExtensionOwner())));
}


/*
 * AddRequestInIndexQueue inserts a record into helio_api_catalog.helio_index_queue
 * for given indexId using SPI.
 */
void
AddRequestInIndexQueue(char *createIndexCmd, int indexId, uint64 collectionId, char
					   cmdType, Oid userOid)
{
	Assert(cmdType == CREATE_INDEX_COMMAND_TYPE || cmdType == REINDEX_COMMAND_TYPE);

	StringInfo cmdStr = makeStringInfo();

	appendStringInfo(cmdStr,
					 "INSERT INTO %s (index_cmd, index_id, collection_id, index_cmd_status, cmd_type, user_oid",
					 GetIndexQueueName());

	appendStringInfo(cmdStr, ") VALUES ($1, $2, $3, $4, $5, $6) ");

	int argCount = 6;
	Oid argTypes[6];
	Datum argValues[6];
	char argNulls[6] = { ' ', ' ', ' ', ' ', ' ', ' ' };

	argTypes[0] = TEXTOID;
	argValues[0] = PointerGetDatum(cstring_to_text(createIndexCmd));

	argTypes[1] = INT4OID;
	argValues[1] = Int32GetDatum(indexId);

	argTypes[2] = INT8OID;
	argValues[2] = Int64GetDatum(collectionId);

	argTypes[3] = INT8OID;
	argValues[3] = Int64GetDatum(IndexCmdStatus_Queued);

	argTypes[4] = CHAROID;
	argValues[4] = CharGetDatum(cmdType);

	argTypes[5] = OIDOID;
	argValues[5] = ObjectIdGetDatum(userOid);

	bool isNull = true;
	bool readOnly = false;

	ExtensionExecuteQueryWithArgsViaSPI(cmdStr->data, argCount, argTypes,
										argValues, argNulls, readOnly,
										SPI_OK_INSERT,
										&isNull);
}


/*
 * GetRequestFromIndexQueue gets the exactly one request corresponding to the collectionId to either for CREATE or REINDEX depending on cmdType.
 */
IndexCmdRequest *
GetRequestFromIndexQueue(char cmdType, uint64 collectionId)
{
	Assert(cmdType == CREATE_INDEX_COMMAND_TYPE || cmdType == REINDEX_COMMAND_TYPE);

	/**
	 * If because of failure scenario, we end up with a index request in "Inprogress"
	 * but there is no backend job really executing the request due to failure.
	 * We should consider such requests as well to be picked. For such requests,
	 * the status will be "Inprogress" but corresponding global_pid will not exist in pg_stat_activity.
	 * The order by clause makes sure that we pick IndexCmdStatus_Queued requests first over other (ascending order).
	 *
	 * SELECT index_cmd, index_id, index_cmd_status,
	 *        COALESCE(attempt, 0) AS attempt, comment, update_time, user_oid
	 * FROM helio_api_catalog.helio_index_queue iq
	 * WHERE cmd_type = '%c'
	 *       AND iq.collection_id = collectionId
	 *       AND (index_cmd_status != IndexCmdStatus_Inprogress
	 *            OR (index_cmd_status = IndexCmdStatus_Inprogress
	 *                AND iq.global_pid IS NOT NULL
	 *                AND citus_pid_for_gpid(iq.global_pid) NOT IN (SELECT distinct pid FROM pg_stat_activity WHERE pid IS NOT NULL)
	 *               )
	 *           )
	 *	ORDER BY index_cmd_status ASC LIMIT 1
	 */
	StringInfo cmdStr = makeStringInfo();
	bool readOnly = false;
	int numValues = 7;
	bool isNull[7];
	Datum results[7];
	Oid userOid = InvalidOid;

	appendStringInfo(cmdStr,
					 "SELECT index_cmd, index_id, index_cmd_status, COALESCE(attempt, 0) AS attempt, comment, update_time, user_oid");
	appendStringInfo(cmdStr,
					 " FROM %s iq WHERE cmd_type = '%c'",
					 GetIndexQueueName(), cmdType);
	appendStringInfo(cmdStr, " AND iq.collection_id = " UINT64_FORMAT, collectionId);
	appendStringInfo(cmdStr, " AND (index_cmd_status != %d", IndexCmdStatus_Inprogress);
	appendStringInfo(cmdStr, " OR (index_cmd_status = %d", IndexCmdStatus_Inprogress);
	appendStringInfo(cmdStr,
					 " AND iq.global_pid IS NOT NULL AND citus_pid_for_gpid(iq.global_pid)");
	appendStringInfo(cmdStr,
					 " NOT IN (SELECT distinct pid FROM pg_stat_activity WHERE pid IS NOT NULL)");
	appendStringInfo(cmdStr, " )) ");
	appendStringInfo(cmdStr,
					 " ORDER BY index_cmd_status ASC LIMIT 1");

	ExtensionExecuteMultiValueQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT, results,
										  isNull, numValues);
	if (isNull[0])
	{
		/* queue has no create index command */
		return NULL;
	}
	char *cmd = text_to_cstring(DatumGetTextP(results[0]));
	int indexId = DatumGetInt32(results[1]);
	int status = DatumGetInt32(results[2]);
	int16 attemptCount = DatumGetUInt16(results[3]);
	pgbson *comment = (pgbson *) DatumGetPointer(results[4]);
	TimestampTz updateTime = DatumGetTimestampTz(results[5]);
	if (!isNull[6])
	{
		userOid = DatumGetObjectId(results[6]);
	}

	IndexCmdRequest *request = palloc(sizeof(IndexCmdRequest));
	request->indexId = indexId;
	request->collectionId = collectionId;
	request->cmd = cmd;
	request->attemptCount = attemptCount;
	request->comment = comment;
	request->updateTime = updateTime;
	request->status = status;
	request->userOid = userOid;
	return request;
}


/*
 * GetCollectionIdsForIndexBuild returns the collectionIds for Index build.
 * This is to make sure that we process only one index build request for a collection at a time.
 */
uint64 *
GetCollectionIdsForIndexBuild(char cmdType, List *excludeCollectionIds)
{
	Assert(cmdType == CREATE_INDEX_COMMAND_TYPE || cmdType == REINDEX_COMMAND_TYPE);

	/* Allocate one more collectionId than MaxNumActiveUsersIndexBuilds so that the last one is always 0 */
	uint64 *collectionIds = palloc0(sizeof(uint64) * (MaxNumActiveUsersIndexBuilds + 1));

	/* For a collectionId also, if there are multiple requests, we first try to get a request which is in Queued state over Failed requests.
	 *
	 * SELECT array_agg(a.collection_id) FROM
	 *  (SELECT collection_id
	 *  FROM helio_api_catalog.helio_index_queue pq
	 *  WHERE cmd_type = $1 AND collection_id <> ANY($2)
	 *  ORDER BY min(pq.index_cmd_status) LIMIT MaxNumActiveUsersIndexBuilds
	 *  ) a;
	 */

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT array_agg(a.collection_id) FROM (");
	appendStringInfo(cmdStr,
					 "SELECT collection_id FROM %s iq WHERE cmd_type = $1",
					 GetIndexQueueName());

	if (excludeCollectionIds != NIL)
	{
		appendStringInfo(cmdStr, " AND collection_id <> ANY($2) ");
	}
	appendStringInfo(cmdStr,
					 " ORDER BY index_cmd_status ASC LIMIT %d",
					 MaxNumActiveUsersIndexBuilds);
	appendStringInfo(cmdStr, ") a");

	int argCount = 1;
	Oid argTypes[2];
	Datum argValues[2];
	char argNulls[2];

	argTypes[0] = CHAROID;
	argValues[0] = CharGetDatum(cmdType);
	argNulls[1] = ' ';

	if (excludeCollectionIds != NIL)
	{
		int64 numCollections = list_length(excludeCollectionIds);
		Datum *collectionIdDatums = palloc0(sizeof(Datum) * numCollections);

		int64 i = 0;
		uint64 collectionId;
		ListCell *cell;
		foreach(cell, excludeCollectionIds)
		{
			collectionId = *(uint64 *) lfirst(cell);
			collectionIdDatums[i] = Int64GetDatum(collectionId);
			i++;
		}

		ArrayType *collectionIdArray = construct_array(collectionIdDatums, numCollections,
													   INT8OID,
													   sizeof(uint64), true,
													   TYPALIGN_INT);
		argTypes[1] = INT8ARRAYOID;
		argValues[1] = PointerGetDatum(collectionIdArray);
		argNulls[1] = ' ';
		argCount++;
	}

	bool isNull = true;
	bool readOnly = true;
	Datum resultDatum = ExtensionExecuteQueryWithArgsViaSPI(cmdStr->data, argCount,
															argTypes,
															argValues, argNulls,
															readOnly,
															SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		/* no request in queue that can be picked */
		return collectionIds;
	}
	ArrayType *array = DatumGetArrayTypeP(resultDatum);

	/* error if any Datums are NULL */
	bool **nulls = NULL;

	Datum *elems = NULL;
	int nelems = 0;
	ArrayExtractDatums(array, INT8OID, &elems, nulls, &nelems);

	Assert(nelems <= MaxNumActiveUsersIndexBuilds);
	for (int i = 0; i < nelems; i++)
	{
		collectionIds[i] = DatumGetInt64(elems[i]);
	}
	return collectionIds;
}


/*
 * RemoveRequestFromIndexQueue deletes the record inserted for given index from
 * helio_api_catalog.helio_index_queue using SPI.
 */
void
RemoveRequestFromIndexQueue(int indexId, char cmdType)
{
	Assert(cmdType == CREATE_INDEX_COMMAND_TYPE || cmdType == REINDEX_COMMAND_TYPE);
	const char *cmdStr = FormatSqlQuery(
		"DELETE FROM %s WHERE index_id = $1 AND cmd_type = $2;", GetIndexQueueName());

	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];
	char argNulls[2] = { ' ', ' ' };

	argTypes[0] = INT4OID;
	argValues[0] = Int32GetDatum(indexId);

	argTypes[1] = CHAROID;
	argValues[1] = CharGetDatum(cmdType);

	bool isNull = true;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr, argCount, argTypes,
										argValues, argNulls, readOnly,
										SPI_OK_DELETE, &isNull);
}


/*
 * MarkIndexRequestStatus sets status, comment for a given Index request and cmdType.
 */
void
MarkIndexRequestStatus(int indexId, char cmdType, IndexCmdStatus status, pgbson *comment,
					   IndexJobOpId *opId, int16 attemptCount)
{
	Assert(cmdType == CREATE_INDEX_COMMAND_TYPE || cmdType == REINDEX_COMMAND_TYPE);
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "UPDATE %s SET index_cmd_status = $1, comment = mongo_catalog.bson_from_bytea($2),"
					 " update_time = $3, attempt = $4 ", GetIndexQueueName());

	if (opId != NULL)
	{
		appendStringInfo(cmdStr, ", global_pid = $7, start_time = $8 ");
	}

	/* We do not want to update the status of a request which is already Skippable.
	 * This is to avoid accidental update of request when long running create index
	 * job(build_index_concurrently) is being cancelled using dropIndexes via pg_backend_cancel(job-pid). This
	 * operation sometimes causes job(build_index_concurrently) to resume and update the status of the same request in later phase.
	 */
	appendStringInfo(cmdStr,
					 " WHERE index_id = $5 AND cmd_type = $6 and index_cmd_status < %d",
					 IndexCmdStatus_Skippable);

	int argCount = 0;
	Oid argTypes[8];
	Datum argValues[8];
	char argNulls[8] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };

	argTypes[0] = INT4OID;
	argValues[0] = Int32GetDatum(status);
	argCount++;

	argTypes[1] = BYTEAOID;
	argValues[1] = PointerGetDatum(CastPgbsonToBytea(comment));
	argCount++;

	argTypes[2] = TIMESTAMPTZOID;
	argValues[2] = TimestampTzGetDatum(GetCurrentTimestamp());
	argCount++;

	argTypes[3] = INT2OID;
	argValues[3] = Int16GetDatum(attemptCount);
	argCount++;

	argTypes[4] = INT4OID;
	argValues[4] = Int32GetDatum(indexId);
	argCount++;

	argTypes[5] = CHAROID;
	argValues[5] = CharGetDatum(cmdType);
	argCount++;

	if (opId != NULL)
	{
		argTypes[6] = INT8OID;
		argValues[6] = Int64GetDatum(opId->global_pid);
		argCount++;

		argTypes[7] = TIMESTAMPTZOID;
		argValues[7] = TimestampTzGetDatum(opId->start_time);
		argCount++;
	}

	bool isNull = true;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr->data, argCount, argTypes,
										argValues, argNulls, readOnly,
										SPI_OK_UPDATE, &isNull);
}


/*
 * GetIndexBuildStatusFromIndexQueue gets the status of Create Index request from the helio_api_catalog.helio_index_queue
 */
IndexCmdStatus
GetIndexBuildStatusFromIndexQueue(int indexId)
{
	const char *cmdStr =
		FormatSqlQuery(
			"SELECT index_cmd_status FROM %s WHERE index_id = $1 AND cmd_type = 'C';",
			GetIndexQueueName());
	int argCount = 1;
	Oid argTypes[1] = { INT4OID };
	Datum argValues[1] = { Int32GetDatum(indexId) };
	char argNulls[1] = { ' ' };

	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("client_min_messages", "WARNING");
	bool readOnly = true;
	bool isNull = true;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(cmdStr, argCount, argTypes,
													   argValues, argNulls, readOnly,
													   SPI_OK_SELECT, &isNull);

	/* rollback the GUC change that we made for client_min_messages */
	RollbackGUCChange(savedGUCLevel);
	if (isNull)
	{
		return IndexCmdStatus_Unknown;
	}
	else
	{
		return (IndexCmdStatus) DatumGetInt64(result);
	}
}


/*
 * Merges the weights field with the set of paths used in the key declaration.
 * If the path is there, it updates the weight. Otherwise, adds the weight
 * as a new path.
 */
List *
MergeTextIndexWeights(List *textIndexes, const bson_value_t *weights, bool *isWildCard)
{
	if (weights->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errmsg("weights must be a document")));
	}

	bson_iter_t weightsIter;
	BsonValueInitIterator(weights, &weightsIter);

	while (bson_iter_next(&weightsIter))
	{
		const char *weightPath = bson_iter_key(&weightsIter);
		const bson_value_t *currentValue = bson_iter_value(&weightsIter);
		if (!BsonValueIsNumber(currentValue))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDINDEXSPECIFICATIONOPTION),
							errmsg("weight for text index needs numeric type")));
		}

		if (isWildCard != NULL && strcmp(weightPath, "$**") == 0)
		{
			*isWildCard = true;
		}

		double weight = BsonValueAsDouble(currentValue);

		ListCell *indexCell;
		bool found = false;
		if (textIndexes != NIL)
		{
			foreach(indexCell, textIndexes)
			{
				TextIndexWeights *weightEntry = lfirst(indexCell);
				if (strcmp(weightEntry->path, weightPath) == 0)
				{
					weightEntry->weight = weight;
					found = true;
					break;
				}
			}
		}

		if (!found)
		{
			TextIndexWeights *weightEntry = palloc0(sizeof(TextIndexWeights));
			weightEntry->path = weightPath;
			weightEntry->weight = weight;
			textIndexes = lappend(textIndexes, weightEntry);
		}
	}

	return textIndexes;
}


/*
 * Returns the index queue name based on the cluster version.
 */
char *
GetIndexQueueName(void)
{
	return "helio_api_catalog.helio_index_queue";
}


/*
 * Returns true if the index plugin vector for the key document is a text index.
 */
inline static bool
IsTextIndex(const bson_value_t *value)
{
	return value->value_type == BSON_TYPE_UTF8 &&
		   strcmp(value->value.v_utf8.str, "text") == 0;
}


/*
 * Serializes the IndexKey to a GetIndexes friendly manner.
 * Returns any additional Text indexes that were captured from processing
 * the index keys.
 */
static List *
WriteIndexKeyForGetIndexes(pgbson_writer *writer, pgbson *keyDocument)
{
	bson_iter_t iter;
	PgbsonInitIterator(keyDocument, &iter);

	bool writtenTextColumn = false;
	pgbson_writer nestedWriter;
	PgbsonWriterStartDocument(writer, "key", 3, &nestedWriter);

	List *fullTextColumns = NIL;

	while (bson_iter_next(&iter))
	{
		StringView keyView = bson_iter_key_string_view(&iter);
		const bson_value_t *value = bson_iter_value(&iter);

		/* We only care about text indexes here.*/
		if (IsTextIndex(value))
		{
			/*
			 * For text, we insert a well-known metadata here
			 * key: { _fts: 'text', _ftsx: 1 }
			 * The actual keys go in "weights as an option"
			 */
			if (!writtenTextColumn)
			{
				PgbsonWriterAppendUtf8(&nestedWriter, "_fts", 4, "text");
				PgbsonWriterAppendInt32(&nestedWriter, "_ftsx", 5, 1);
				writtenTextColumn = true;
			}

			/* Store the columns for later (weights) */
			TextIndexWeights *weights = palloc0(sizeof(TextIndexWeights));
			weights->path = keyView.string;
			weights->weight = 1.0;
			fullTextColumns = lappend(fullTextColumns, (void *) weights);
		}
		else
		{
			PgbsonWriterAppendValue(&nestedWriter, keyView.string, keyView.length, value);
		}
	}

	PgbsonWriterEndDocument(writer, &nestedWriter);
	return fullTextColumns;
}


/*
 * Checks if Index keys are equivalent. The keys are equal if everything
 * matches. They are equivalent if one index key can be treated as the same
 * index key to another index even if it's not an exact match (e.g. text indexes).
 * Otherwise, the keys are not equal.
 */
static IndexOptionsEquivalency
IndexKeyDocumentEquivalent(pgbson *leftKey, pgbson *rightKey)
{
	if (PgbsonEquals(leftKey, rightKey))
	{
		return IndexOptionsEquivalency_Equal;
	}

	/*
	 * Here we check for multiple things:
	 * If left or right has at least one text index, they're automatically equivalent.
	 * Are the keys/values unequal - then not equivalent
	 * If count(left keys) != count(right keys) then not equivalent.
	 */
	bool leftHasTextIndexes = false;
	bool rightHasTextIndexes = false;
	bool areEqual = true;

	/* The equivalency matters for text indexes */
	bson_iter_t leftIter;
	bson_iter_t rightIter;
	PgbsonInitIterator(leftKey, &leftIter);
	PgbsonInitIterator(rightKey, &rightIter);
	bool leftHasNext = bson_iter_next(&leftIter);
	bool rightHasNext = bson_iter_next(&rightIter);

	while (leftHasNext && rightHasNext)
	{
		if (areEqual && strcmp(bson_iter_key(&leftIter), bson_iter_key(&rightIter)) != 0)
		{
			areEqual = false;
		}

		const bson_value_t *leftValue = bson_iter_value(&leftIter);
		const bson_value_t *rightValue = bson_iter_value(&rightIter);

		if (areEqual && !BsonValueEquals(leftValue, rightValue))
		{
			areEqual = false;
		}

		leftHasTextIndexes = leftHasTextIndexes || IsTextIndex(leftValue);
		rightHasTextIndexes = rightHasTextIndexes || IsTextIndex(rightValue);

		leftHasNext = bson_iter_next(&leftIter);
		rightHasNext = bson_iter_next(&rightIter);
	}

	while (leftHasNext)
	{
		areEqual = false;
		const bson_value_t *leftValue = bson_iter_value(&leftIter);
		leftHasTextIndexes = leftHasTextIndexes || IsTextIndex(leftValue);
		leftHasNext = bson_iter_next(&leftIter);
	}

	while (rightHasNext)
	{
		areEqual = false;
		const bson_value_t *rightValue = bson_iter_value(&rightIter);
		rightHasTextIndexes = rightHasTextIndexes || IsTextIndex(rightValue);
		rightHasNext = bson_iter_next(&rightIter);
	}

	if (areEqual)
	{
		return IndexOptionsEquivalency_Equal;
	}

	if (leftHasTextIndexes && rightHasTextIndexes)
	{
		/*
		 * If two indexes have 'text' - no matter if there's other
		 * indexes, they're considered equivalent
		 */
		return IndexOptionsEquivalency_TextEquivalent;
	}

	return IndexOptionsEquivalency_NotEquivalent;
}


/*
 * Serializes the IndexSpec to a bson compatible response.
 * For GetIndexes this formats it into a user friendly version.
 * Otherwise (for Shard_collection etc.) it retains the original format.
 */
static pgbson *
SerializeIndexSpec(const IndexSpec *indexSpec, bool isGetIndexes,
				   const char *namespaceName)
{
	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	PgbsonWriterAppendInt32(&finalWriter, "v", 1, indexSpec->indexVersion);

	List *textColumns = NIL;
	const char *languageOverride = "language";
	if (isGetIndexes)
	{
		/* For GetIndexes, we may need to rewrite the key if there's non-regular indexes (e.g. text ) */
		textColumns = WriteIndexKeyForGetIndexes(&finalWriter,
												 indexSpec->indexKeyDocument);
	}
	else
	{
		PgbsonWriterAppendDocument(&finalWriter, "key", 3, indexSpec->indexKeyDocument);
	}

	PgbsonWriterAppendUtf8(&finalWriter, "name", 4, indexSpec->indexName);

	if (indexSpec->indexPFEDocument != NULL)
	{
		PgbsonWriterAppendDocument(&finalWriter, "partialFilterExpression", -1,
								   indexSpec->indexPFEDocument);
	}

	if (indexSpec->indexWPDocument != NULL)
	{
		PgbsonWriterAppendDocument(&finalWriter, "wildcardProjection", -1,
								   indexSpec->indexWPDocument);
	}

	if (indexSpec->indexSparse != BoolIndexOption_Undefined)
	{
		PgbsonWriterAppendBool(&finalWriter, "sparse", 6, indexSpec->indexSparse ==
							   BoolIndexOption_True);
	}

	if (indexSpec->indexUnique != BoolIndexOption_Undefined)
	{
		/*
		 * Unique is only tracked for getIndexes if it's explicitly set to true.
		 */
		if (!isGetIndexes || indexSpec->indexUnique == BoolIndexOption_True)
		{
			PgbsonWriterAppendBool(&finalWriter, "unique", 6, indexSpec->indexUnique ==
								   BoolIndexOption_True);
		}
	}

	if (indexSpec->indexExpireAfterSeconds != NULL)
	{
		PgbsonWriterAppendInt32(&finalWriter, "expireAfterSeconds", -1,
								*indexSpec->indexExpireAfterSeconds);
	}

	if (indexSpec->cosmosSearchOptions != NULL)
	{
		bson_iter_t bsonIterator;
		if (PgbsonInitIteratorAtPath(indexSpec->cosmosSearchOptions, "", &bsonIterator))
		{
			PgbsonWriterAppendValue(&finalWriter,
									"cosmosSearchOptions",
									-1,
									bson_iter_value(&bsonIterator));
		}
	}

	if (indexSpec->indexOptions != NULL)
	{
		if (textColumns != NIL)
		{
			bson_iter_t optionsIter;
			PgbsonInitIterator(indexSpec->indexOptions, &optionsIter);

			while (bson_iter_next(&optionsIter))
			{
				StringView keyView = bson_iter_key_string_view(&optionsIter);
				if (StringViewEqualsCString(&keyView, "weights"))
				{
					bool isWildCardIgnore = false;
					textColumns = MergeTextIndexWeights(textColumns, bson_iter_value(
															&optionsIter),
														&isWildCardIgnore);
				}
				else if (StringViewEqualsCString(&keyView, "language_override"))
				{
					languageOverride = bson_iter_utf8(&optionsIter, NULL);
				}
				else
				{
					PgbsonWriterAppendValue(&finalWriter, keyView.string, keyView.length,
											bson_iter_value(&optionsIter));
				}
			}
		}
		else
		{
			PgbsonWriterConcat(&finalWriter, indexSpec->indexOptions);
		}
	}


	if (textColumns != NIL)
	{
		pgbson_writer weightsWriter;

		/*
		 * Write out the columns as weights
		 * weights: { a: 1, b: 1 }
		 */
		PgbsonWriterStartDocument(&finalWriter, "weights", -1, &weightsWriter);
		ListCell *cell;
		foreach(cell, textColumns)
		{
			TextIndexWeights *column = lfirst(cell);
			PgbsonWriterAppendDouble(&weightsWriter, column->path, -1, column->weight);
		}

		PgbsonWriterEndDocument(&finalWriter, &weightsWriter);

		/* TODO: Fill in collation data, language_override
		 */
		PgbsonWriterAppendInt32(&finalWriter, "textIndexVersion", -1, 2);
		PgbsonWriterAppendUtf8(&finalWriter, "language_override", -1, languageOverride);
	}

	if (namespaceName != NULL)
	{
		PgbsonWriterAppendUtf8(&finalWriter, "ns", 2, namespaceName);
	}

	return PgbsonWriterGetPgbson(&finalWriter);
}
