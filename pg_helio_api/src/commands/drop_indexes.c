/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/drop_indexes.c
 *
 * Implementation of the drop index operation.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <access/xact.h>
#include <catalog/namespace.h>
#include <commands/sequence.h>
#include <executor/spi.h>
#include <fmgr.h>
#include <funcapi.h>
#include <lib/stringinfo.h>
#include <nodes/pg_list.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>

#include "api_hooks.h"
#include "io/helio_bson_core.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "commands/drop_indexes.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "metadata/index.h"
#include "utils/query_utils.h"
#include "utils/index_utils.h"


typedef enum
{
	DROP_INDEX_MODE_INVALID = 0,
	DROP_INDEX_BY_NAME_LIST,
	DROP_INDEX_BY_SPEC_DOCUMENT
} DropIndexMode;


/* Represents whole "arg" document passed to dbcommand/dropIndexes */
typedef struct
{
	/* represents value of "dropIndexes" field */
	char *collectionName;

	/* represents value of "index" field */
	DropIndexMode dropIndexMode;
	union
	{
		List *nameList;   /* DROP_INDEX_BY_NAME_LIST */
		pgbson *document; /* DROP_INDEX_BY_SPEC_DOCUMENT */
	} index;


	/* TODO: other things such as writeConcern, comment ... */
} DropIndexesArg;

/*
 * Contains the data used when building the bson object that needs to be
 * sent to the client after a dropIndexes() command.
 */
typedef struct
{
	bool ok;
	int64 nIndexesWas;

	/* error reporting; valid only when "ok" is false */
	char *errmsg;
	int errcode;
} DropIndexesResult;


#define DROP_INDEX_ARG_FIELD_NOT_IMPL \
	"\"%s\" field for drop index arg is not implemented"


PG_FUNCTION_INFO_V1(command_drop_indexes);
PG_FUNCTION_INFO_V1(command_drop_indexes_concurrently);
PG_FUNCTION_INFO_V1(command_drop_indexes_concurrently_internal);

static DropIndexesArg ParseDropIndexesArg(pgbson *options);
static void DropIndexesArgExpandIndexNameList(uint64 collectionId,
											  DropIndexesArg *dropIndexesArg);
static DropIndexesResult * DropIndexesConcurrentlyInternal(char *dbName, pgbson *arg);
static DropIndexesResult ProcessDropIndexesRequest(char *dbName, DropIndexesArg
												   dropIndexesArg,
												   bool dropIndexConcurrently);
static pgbson * MakeDropIndexesMsg(DropIndexesResult *result);
static void ExecuteDropIndexCommand(char *cmd, bool unique, bool concurrently);
static char * CreateDropIndexCommand(uint64 collectionId, int indexId, bool unique, bool
									 concurrently,
									 bool missingOk);
static void HandleDropIndexConcurrently(uint64 collectionId, int indexId, bool unique,
										bool concurrently,
										bool missingOk, DropIndexesResult *result,
										MemoryContext oldMemContext);
static void CleanUpIndexBuildRequest(int indexId);

/*
 * command_drop_indexes is the implementation of the internal logic for
 * dbcommand/dropIndexes.
 */
Datum
command_drop_indexes(PG_FUNCTION_ARGS)
{
	/* mongo_api_v1.drop_indexes already verified NULL args but .. */
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("dbName cannot be NULL")));
	}
	char *dbName = text_to_cstring(PG_GETARG_TEXT_P(0));

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("arg cannot be NULL")));
	}
	pgbson *arg = PG_GETARG_PGBSON(1);

	DropIndexesArg dropIndexesArg = ParseDropIndexesArg(arg);

	bool dropIndexConcurrently = false;
	DropIndexesResult dropIndexResult = ProcessDropIndexesRequest(dbName, dropIndexesArg,
																  dropIndexConcurrently);
	Datum values[1] = { 0 };
	bool isNulls[1] = { false };
	values[0] = PointerGetDatum(MakeDropIndexesMsg(&dropIndexResult));

	/* fetch TupleDesc for result, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);
	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(resultTupDesc, values, isNulls)));
}


/*
 * ProcessDropIndexesRequest is the implementation of the internal logic for
 * dropIndexes. If dropIndexConcurrently is set to true, then drop index concurrently.
 */
static DropIndexesResult
ProcessDropIndexesRequest(char *dbName, DropIndexesArg dropIndexesArg, bool
						  dropIndexConcurrently)
{
	char *collectionName = dropIndexesArg.collectionName;
	MongoCollection *collection =
		GetMongoCollectionByNameDatum(PointerGetDatum(cstring_to_text(dbName)),
									  PointerGetDatum(cstring_to_text(collectionName)),
									  AccessShareLock);
	if (collection == NULL)
	{
		ereport(ERROR, (errcode(MongoNamespaceNotFound),
						errmsg("ns not found %s.%s", dbName, collectionName)));
	}

	uint64 collectionId = collection->collectionId;

	DropIndexesResult result = {
		.ok = true,
		.nIndexesWas = CollectionIdGetIndexCount(collectionId)
	};

	if (dropIndexesArg.dropIndexMode == DROP_INDEX_BY_NAME_LIST)
	{
		/* expand "*" to a list of index names to be dropped, if provided */
		DropIndexesArgExpandIndexNameList(collectionId, &dropIndexesArg);

		/* TODO: maybe sort dropIndexNameList before moving forward ? */
		ListCell *indexNameCell = NULL;
		List *indexesDetailsList = NIL;
		foreach(indexNameCell, dropIndexesArg.index.nameList)
		{
			char *indexName = lfirst(indexNameCell);

			if (strcmp(indexName, ID_INDEX_NAME) == 0)
			{
				ereport(ERROR, (errcode(MongoInvalidOptions),
								errmsg("cannot drop _id index")));
			}

			IndexDetails *indexDetails = IndexNameGetIndexDetails(collectionId,
																  indexName);
			if (indexDetails == NULL)
			{
				ereport(ERROR, (errcode(MongoIndexNotFound),
								errmsg("index not found with name [%s]", indexName)));
			}

			bool missingOk = true;
			bool concurrently = false;
			if (!dropIndexConcurrently)
			{
				DropPostgresIndex(collectionId, indexDetails->indexId,
								  indexDetails->indexSpec.indexUnique ==
								  BoolIndexOption_True,
								  concurrently, missingOk);
				DeleteCollectionIndexRecord(collectionId, indexDetails->indexId);
			}
			else
			{
				/* Mark index entries as skippable before starting the clean up process
				 * This is to avoid accidental pick up of request by the cron job while we are cleaning up index.
				 * BugId 2858365
				 */
				pgbson *emptyComment = PgbsonInitEmpty();
				MarkIndexRequestStatus(indexDetails->indexId, CREATE_INDEX_COMMAND_TYPE,
									   IndexCmdStatus_Skippable, emptyComment, NULL, 1);
			}
			indexesDetailsList = lappend(indexesDetailsList, indexDetails);
		}

		/* We need to do all prevalidation on all given index names and
		* if none of them fails then only we should start dropping indexes.
		* In Native MongoDB, we see that none of indexes are dropped
		* if there are validation issues like 'index not found' etc. */
		if (dropIndexConcurrently && indexesDetailsList != NIL)
		{
			ListCell *indexDetailsCell = NULL;
			bool missingOk = true;
			bool concurrently = true;

			/* save the memory context before committing the transaction */
			MemoryContext oldMemContext = CurrentMemoryContext;

			/* commit here so that MarkIndexRequestStatus is visible */
			PopAllActiveSnapshots();
			CommitTransactionCommand();
			StartTransactionCommand();

			foreach(indexDetailsCell, indexesDetailsList)
			{
				const IndexDetails *indexDetails = (IndexDetails *) lfirst(
					indexDetailsCell);
				HandleDropIndexConcurrently(collectionId, indexDetails->indexId,
											indexDetails->indexSpec.indexUnique ==
											BoolIndexOption_True,
											concurrently, missingOk, &result,
											oldMemContext);
				if (!result.ok)
				{
					/* break foreach */
					break;
				}
			}
		}
	}
	else if (dropIndexesArg.dropIndexMode == DROP_INDEX_BY_SPEC_DOCUMENT)
	{
		/*
		 * IndexKeyGetMatchingValidIndexes returns indexes in the order of their
		 * ids, so we don't need to worry about regression tests that report
		 * conflicting indexes becoming flaky.
		 */
		List *matchingIndexDetailsList =
			IndexKeyGetMatchingIndexes(collectionId,
									   dropIndexesArg.index.document);
		int nmatchingIndexes = list_length(matchingIndexDetailsList);
		if (nmatchingIndexes == 0)
		{
			const char *keyDocumentStr =
				PgbsonToJsonForLogging(dropIndexesArg.index.document);

			ereport(ERROR, (errcode(MongoIndexNotFound),
							errmsg("can't find index with key: %s", keyDocumentStr)));
		}
		else if (nmatchingIndexes > 1)
		{
			const char *keyDocumentStr =
				PgbsonToJsonForLogging(dropIndexesArg.index.document);

			IndexDetails *firstMatchingIndexDetails =
				linitial(matchingIndexDetailsList);
			const char *firstMatchingIndexSpecStr = PgbsonToJsonForLogging(
				IndexSpecAsBson(&firstMatchingIndexDetails->indexSpec));

			IndexDetails *secondMatchingIndexDetails =
				lsecond(matchingIndexDetailsList);
			const char *secondMatchingIndexSpecStr = PgbsonToJsonForLogging(
				IndexSpecAsBson(&secondMatchingIndexDetails->indexSpec));

			ereport(ERROR, (errcode(MongoAmbiguousIndexKeyPattern),
							errmsg("%d indexes found for key: %s, identify by "
								   "name instead. Conflicting indexes: %s, %s",
								   nmatchingIndexes, keyDocumentStr,
								   firstMatchingIndexSpecStr,
								   secondMatchingIndexSpecStr)));
		}

		IndexDetails *matchingIndexDetails = linitial(matchingIndexDetailsList);
		if (strcmp(matchingIndexDetails->indexSpec.indexName, ID_INDEX_NAME) == 0)
		{
			ereport(ERROR, (errcode(MongoInvalidOptions),
							errmsg("cannot drop _id index")));
		}

		bool missingOk = true;
		bool concurrently = false;
		if (dropIndexConcurrently)
		{
			concurrently = true;

			/* Mark index entries as skippable before starting the clean up process
			 * This is to avoid accidental pick up of request by the cron job while we are cleaning up index.
			 * BugId 2858365
			 */
			pgbson *emptyComment = PgbsonInitEmpty();
			MarkIndexRequestStatus(matchingIndexDetails->indexId,
								   CREATE_INDEX_COMMAND_TYPE,
								   IndexCmdStatus_Skippable, emptyComment, NULL, 1);

			/* save the memory context before committing the transaction */
			MemoryContext oldMemContext = CurrentMemoryContext;

			/* commit here so that MarkIndexRequestStatus is visible */
			PopAllActiveSnapshots();
			CommitTransactionCommand();
			StartTransactionCommand();

			HandleDropIndexConcurrently(collectionId, matchingIndexDetails->indexId,
										matchingIndexDetails->indexSpec.indexUnique ==
										BoolIndexOption_True,
										concurrently, missingOk, &result,
										oldMemContext);
		}
		else
		{
			DropPostgresIndex(collectionId, matchingIndexDetails->indexId,
							  matchingIndexDetails->indexSpec.indexUnique ==
							  BoolIndexOption_True,
							  concurrently, missingOk);
			DeleteCollectionIndexRecord(collectionId, matchingIndexDetails->indexId);
		}
	}
	else
	{
		ereport(ERROR, (errmsg("unexpected drop index mode")));
	}

	return result;
}


/*
 * command_drop_indexes_concurrently_internal calls command_drop_indexes_concurrently_internal only on coordinator.
 */
Datum
command_drop_indexes_concurrently(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("dbName cannot be NULL")));
	}
	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("arg cannot be NULL")));
	}
	text *databaseDatum = PG_GETARG_TEXT_P(0);
	pgbson *spec = PG_GETARG_PGBSON(1);
	bool ok = false;
	pgbson *respose;

	if (IsMetadataCoordinator())
	{
		DropIndexesResult *result = DropIndexesConcurrentlyInternal(text_to_cstring(
																		databaseDatum),
																	spec);
		respose = MakeDropIndexesMsg(result);
	}
	else
	{
		StringInfo query = makeStringInfo();
		appendStringInfo(query,
						 "CALL %s.drop_indexes_concurrently_internal(%s,%s)",
						 ApiInternalSchemaName,
						 quote_literal_cstr(text_to_cstring(databaseDatum)),
						 quote_literal_cstr(PgbsonToHexadecimalString(spec)));

		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(query->data);
		if (!result.success)
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg(
								"Error submitting background index/ drop index %s",
								text_to_cstring(result.response)),
							errhint(
								"Error submitting index request/drop index %s",
								text_to_cstring(result.response))));
		}

		char *responseChar = text_to_cstring(result.response);
		if (IsBsonHexadecimalString(responseChar))
		{
			respose = PgbsonInitFromHexadecimalString(responseChar);
		}
		else
		{
			/* It's a json string use json deserialization */
			respose = PgbsonInitFromJson(responseChar);
		}
	}

	bson_iter_t documentIter;
	PgbsonInitIterator(respose, &documentIter);
	while (bson_iter_next(&documentIter))
	{
		const char *key = bson_iter_key(&documentIter);
		if (strcmp(key, "ok") == 0)
		{
			const bson_value_t *value = bson_iter_value(&documentIter);
			ok = value->value.v_bool;
			break;
		}
	}

	Datum values[2] = { 0 };
	bool isNulls[2] = { false, false };
	values[0] = PointerGetDatum(respose);
	values[1] = BoolGetDatum(ok);

	/* fetch TupleDesc for result, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	return HeapTupleGetDatum(heap_form_tuple(resultTupDesc, values, isNulls));
}


/*
 * command_drop_indexes_concurrently_internal drop index concurrently and also cancels the running create index request for the same index.
 */
Datum
command_drop_indexes_concurrently_internal(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("dbName cannot be NULL")));
	}
	char *dbName = text_to_cstring(PG_GETARG_TEXT_P(0));

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("arg cannot be NULL")));
	}
	pgbson *arg = PG_GETARG_PGBSON(1);

	DropIndexesResult *dropIndexResult = DropIndexesConcurrentlyInternal(dbName, arg);

	Datum values[1] = { 0 };
	bool isNulls[1] = { false };
	values[0] = PointerGetDatum(MakeDropIndexesMsg(dropIndexResult));

	/* fetch TupleDesc for result, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	PG_RETURN_DATUM(HeapTupleGetDatum(heap_form_tuple(resultTupDesc, values,
													  isNulls)));
}


/* DropIndexesConcurrentlyInternal drops index concurrently and also cancels the running create index request for the same index. */
static DropIndexesResult *
DropIndexesConcurrentlyInternal(char *dbName, pgbson *arg)
{
	DropIndexesResult *dropIndexResult = palloc0(sizeof(DropIndexesResult));

	MemoryContext savedMemoryContext = CurrentMemoryContext;
	PG_TRY();
	{
		DropIndexesArg dropIndexesArg = ParseDropIndexesArg(arg);
		bool dropIndexConcurrently = true;
		*dropIndexResult = ProcessDropIndexesRequest(dbName, dropIndexesArg,
													 dropIndexConcurrently);
	}
	PG_CATCH();
	{
		/* Since run_command_on_coordinator does not return error code in case of failure inside the called function
		 * i.e. mongo_api_internal.drop_indexes_concurrently_internal in this case.
		 * The way we are solving it by adding 'errmsg' and 'code' in the DropIndexesResult and
		 * setting them (with ok->false) when there is any exception in here.
		 */
		MemoryContextSwitchTo(savedMemoryContext);
		dropIndexResult->ok = false;
		dropIndexResult->nIndexesWas = 0;

		ErrorData *edata = CopyErrorDataAndFlush();
		dropIndexResult->errcode = edata->sqlerrcode;
		dropIndexResult->errmsg = edata->message;
	}
	PG_END_TRY();
	return dropIndexResult;
}


/*
 * ParseDropIndexesArg returns a DropIndexesArg object by parsing given
 * pgbson object that represents the "arg" document passed to
 * dbCommand/dropIndexes.
 */
static DropIndexesArg
ParseDropIndexesArg(pgbson *arg)
{
	DropIndexesArg dropIndexesArg = { 0 };

	/*
	 * Distinguish "index: []" from not specifying "index" field at all.
	 * While the former one is ok, the later one is not.
	 */
	bool gotIndexObject = false;

	bson_iter_t argIter;
	PgbsonInitIterator(arg, &argIter);
	while (bson_iter_next(&argIter))
	{
		/*
		 * As Mongo does, we don't throw an error if we encounter with
		 * definition of a field more than once.
		 *
		 * TODO: We should also not throw an error if there is a syntax error
		 *       in the preceding definitions. That means, even if the first
		 *       definiton of "index" uses an integer, we should not throw an
		 *       error for the following, since later definition is valid:
		 *       {"index": 1, "index": "my_index_name"}
		 */

		const char *argKey = bson_iter_key(&argIter);

		/*
		 * "deleteIndexes" command is deprecated but is still supported by Mongo v5
		 * such that it is treated same as "dropIndexes" command. The bson message
		 * that it takes and the error messages thrown are based on "dropIndexes",
		 * so we don't need to do anything specific to support that command.
		 */
		if (strcmp(argKey, "dropIndexes") == 0 ||
			strcmp(argKey, "deleteIndexes") == 0)
		{
			EnsureTopLevelFieldType("dropIndexes.dropIndexes", &argIter, BSON_TYPE_UTF8);

			const bson_value_t *dropIndexesVal = bson_iter_value(&argIter);
			dropIndexesArg.collectionName = pstrdup(dropIndexesVal->value.v_utf8.str);
		}
		else if (strcmp(argKey, "index") == 0)
		{
			gotIndexObject = true;
			dropIndexesArg.dropIndexMode = DROP_INDEX_MODE_INVALID;

			if (BSON_ITER_HOLDS_UTF8(&argIter))
			{
				const bson_value_t *indexVal = bson_iter_value(&argIter);
				dropIndexesArg.index.nameList =
					list_make1(pstrdup(indexVal->value.v_utf8.str));
				dropIndexesArg.dropIndexMode = DROP_INDEX_BY_NAME_LIST;
			}
			else if (BSON_ITER_HOLDS_DOCUMENT(&argIter))
			{
				dropIndexesArg.index.document = PgbsonInitFromIterDocumentValue(&argIter);
				dropIndexesArg.dropIndexMode = DROP_INDEX_BY_SPEC_DOCUMENT;
			}
			else if (BSON_ITER_HOLDS_ARRAY(&argIter))
			{
				dropIndexesArg.index.nameList = NIL;
				dropIndexesArg.dropIndexMode = DROP_INDEX_BY_NAME_LIST;

				bson_iter_t indexArrayIter;
				bson_iter_recurse(&argIter, &indexArrayIter);
				while (bson_iter_next(&indexArrayIter))
				{
					EnsureTopLevelFieldType("dropIndexes.index.item", &indexArrayIter,
											BSON_TYPE_UTF8);

					const bson_value_t *indexArrayVal = bson_iter_value(&indexArrayIter);
					dropIndexesArg.index.nameList =
						lappend(dropIndexesArg.index.nameList,
								pstrdup(indexArrayVal->value.v_utf8.str));
				}
			}
			else
			{
				ThrowTopLevelTypeMismatchError("dropIndexes.index",
											   BsonIterTypeName(&argIter),
											   "[string, object]");
			}
		}
		else if (IsCommonSpecIgnoredField(argKey))
		{
			elog(DEBUG1, "Unrecognized command field: dropIndexes.%s", argKey);

			/*
			 *  Silently ignore now, so that clients don't break
			 *  TODO: implement me
			 *      writeConcern
			 *      comment
			 */
		}
		else
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg("BSON field 'dropIndexes.%s' is an unknown field",
								   argKey)));
		}
	}

	/* verify that all non-optional fields are given */

	if (dropIndexesArg.collectionName == NULL)
	{
		ThrowTopLevelMissingFieldError("dropIndexes.dropIndexes");
	}

	if (!gotIndexObject)
	{
		ThrowTopLevelMissingFieldError("dropIndexes.index");
	}
	else if (dropIndexesArg.dropIndexMode == DROP_INDEX_BY_NAME_LIST &&
			 list_length(dropIndexesArg.index.nameList) == 0)
	{
		/* Mongo allows passing an empty array too, so this is ok */
	}

	return dropIndexesArg;
}


/*
 * DropIndexesArgExpandIndexNameList rewrites index.indexNameList so
 * that it contains names of all the indexes that given collection has
 * (except _id) if first element of the list is "*".
 *
 * However, if "*" is placed in a different position, then we won't do so.
 * This is because, in that case we should throw an error complaining that
 * there is no such index since it's already not allowed to define an index
 * with name "*".
 */
static void
DropIndexesArgExpandIndexNameList(uint64 collectionId, DropIndexesArg *dropIndexesArg)
{
	Assert(dropIndexesArg->dropIndexMode == DROP_INDEX_BY_NAME_LIST);

	if (list_length(dropIndexesArg->index.nameList) != 0 &&
		strcmp(linitial(dropIndexesArg->index.nameList), "*") == 0)
	{
		/* we don't want to drop _id index */
		bool excludeIdIndex = true;
		bool inProgressOnly = false;
		dropIndexesArg->index.nameList = CollectionIdGetIndexNames(collectionId,
																   excludeIdIndex,
																   inProgressOnly);
	}
}


/*
 * DropPostgresIndex drops GIN index that belongs to Mongo index with indexId.
 */
void
DropPostgresIndex(uint64 collectionId, int indexId, bool unique, bool concurrently,
				  bool missingOk)
{
	char *cmd = CreateDropIndexCommand(collectionId, indexId, unique, concurrently,
									   missingOk);
	ExecuteDropIndexCommand(cmd, unique, concurrently);
}


/*
 * Drop indexes where names can have suffix and also considers default id index for drop
 *
 * Callers should be careful not to drop valid index, once an index is identifies as invalid
 * then only this should be called
 *
 * Currenlty used by re-index
 */
void
DropPostgresIndexWithSuffix(uint64 collectionId, IndexDetails *index, bool concurrently,
							bool missingOk, const char *suffix)
{
	Assert(suffix != NULL);

	StringInfo cmdStr = makeStringInfo();
	bool isUnique = GetBoolFromBoolIndexOption(index->indexSpec.indexUnique);
	if (isUnique || (strncmp(index->indexSpec.indexName,
							 ID_INDEX_NAME, strlen(ID_INDEX_NAME)) == 0))
	{
		/* These are constraints */
		appendStringInfo(cmdStr,
						 "ALTER TABLE %s." MONGO_DATA_TABLE_NAME_FORMAT
						 " DROP CONSTRAINT %s ", ApiDataSchemaName, collectionId,
						 missingOk ? "IF EXISTS" : "");
		if (isUnique)
		{
			appendStringInfo(cmdStr,
							 MONGO_DATA_TABLE_INDEX_NAME_FORMAT "%s",
							 index->indexId, suffix);
		}
		else
		{
			appendStringInfo(cmdStr,
							 MONGO_DATA_PRIMARY_KEY_FORMAT_PREFIX UINT64_FORMAT "%s",
							 collectionId, suffix);
		}

		bool isNull = true;
		bool readOnly = false;
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY, &isNull);
		Assert(isNull);
	}
	else
	{
		/* These are indexes */
		appendStringInfo(cmdStr,
						 "DROP INDEX %s %s %s."
						 MONGO_DATA_TABLE_INDEX_NAME_FORMAT "%s",
						 concurrently ? "CONCURRENTLY" : "",
						 missingOk ? "IF EXISTS" : "", ApiDataSchemaName, index->indexId,
						 suffix);

		if (concurrently)
		{
			ExtensionExecuteQueryOnLocalhostViaLibPQ(cmdStr->data);
		}
		else
		{
			bool isNull = true;
			bool readOnly = false;
			ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY, &isNull);
			Assert(isNull);
		}
	}
}


/*
 * MakeDropIndexesMsg returns a bson object that encapsulates given
 * DropIndexesResult object.
 */
static pgbson *
MakeDropIndexesMsg(DropIndexesResult *result)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendBool(&writer, "ok", strlen("ok"), result->ok);

	if (result->ok)
	{
		PgbsonWriterAppendInt64(&writer, "nIndexesWas", strlen("nIndexesWas"),
								result->nIndexesWas);
	}
	else
	{
		PgbsonWriterAppendUtf8(&writer, "errmsg", strlen("errmsg"), result->errmsg);
		PgbsonWriterAppendInt32(&writer, "code", strlen("code"), result->errcode);
	}
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Prepares Postgres Drop Index command based on the parameters like unique, concurrently.
 */
static char *
CreateDropIndexCommand(uint64 collectionId, int indexId, bool unique, bool concurrently,
					   bool missingOk)
{
	StringInfo cmdStr = makeStringInfo();

	if (unique)
	{
		appendStringInfo(cmdStr,
						 "ALTER TABLE %s." MONGO_DATA_TABLE_NAME_FORMAT
						 " DROP CONSTRAINT %s " MONGO_DATA_TABLE_INDEX_NAME_FORMAT,
						 ApiDataSchemaName, collectionId, missingOk ? "IF EXISTS" : "",
						 indexId);
	}
	else
	{
		appendStringInfo(cmdStr,
						 "DROP INDEX %s %s %s."
						 MONGO_DATA_TABLE_INDEX_NAME_FORMAT,
						 concurrently ? "CONCURRENTLY" : "",
						 missingOk ? "IF EXISTS" : "", ApiDataSchemaName, indexId);
	}
	return cmdStr->data;
}


/*
 * Actually executes DROP INDEX postgres command.
 */
static void
ExecuteDropIndexCommand(char *cmd, bool unique, bool concurrently)
{
	if (unique)
	{
		bool isNull = true;
		bool readOnly = false;
		ExtensionExecuteQueryViaSPI(cmd, readOnly, SPI_OK_UTILITY, &isNull);
		Assert(isNull);
	}
	else
	{
		if (concurrently)
		{
			ExtensionExecuteQueryOnLocalhostViaLibPQ(cmd);
		}
		else
		{
			bool isNull = true;
			bool readOnly = false;
			ExtensionExecuteQueryViaSPI(cmd, readOnly, SPI_OK_UTILITY, &isNull);
			Assert(isNull);
		}
	}
}


/*
 * HandleDropIndexConcurrently handles dropping index with concurrently
 */
static void
HandleDropIndexConcurrently(uint64 collectionId, int indexId, bool unique, bool
							concurrently, bool missingOk, DropIndexesResult *result,
							MemoryContext oldMemContext)
{
	/* Clean up existing running create index job. */
	CleanUpIndexBuildRequest(indexId);

	/*
	 * We commit the transaction to prevent concurrent index creation
	 * getting blocked on that transaction due to any snapshots that
	 * we might have grabbed so far.
	 */
	PopAllActiveSnapshots();
	CommitTransactionCommand();
	StartTransactionCommand();

	/* declared volatile because of the longjmp in PG_CATCH */
	volatile bool indexDropped = false;
	PG_TRY();
	{
		char *cmd = CreateDropIndexCommand(collectionId, indexId, unique, concurrently,
										   missingOk);
		ExecuteDropIndexCommand(cmd, unique, concurrently);
		indexDropped = true;
	}
	PG_CATCH();
	{
		/* save error info into right context */
		MemoryContextSwitchTo(oldMemContext);
		ErrorData *edata = CopyErrorDataAndFlush();
		result->errcode = edata->sqlerrcode;
		result->errmsg = edata->message;
		result->ok = false;

		ereport(DEBUG1, (errmsg("couldn't drop some indexes for a collection")));

		PopAllActiveSnapshots();
		AbortCurrentTransaction();
		StartTransactionCommand();
	}
	PG_END_TRY();

	if (indexDropped)
	{
		DeleteCollectionIndexRecord(collectionId, indexId);
	}
}


/*
 * CleanUpIndexBuildRequest cleans up an existing (if there is any) running Index Build request for the indexId.
 * It also deletes the Index Build request from ApiCatalogSchemaName.ExtensionObjectPrefix_index_queue
 */
static void
CleanUpIndexBuildRequest(int indexId)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "WITH op_data AS (SELECT citus_pid_for_gpid(iq.global_pid) AS pid, iq.global_pid AS global_pid, iq.start_time AS timestamp");
	appendStringInfo(cmdStr,
					 " FROM %s.%s_index_queue iq WHERE index_id = %d AND cmd_type = '%c')",
					 ApiCatalogSchemaName, ExtensionObjectPrefix, indexId,
					 CREATE_INDEX_COMMAND_TYPE);
	appendStringInfo(cmdStr,
					 ", matching_backends AS ( SELECT op_data.pid AS pid, op_data.global_pid AS global_pid, op_data.timestamp FROM op_data "
					 " JOIN pg_stat_activity csa ON op_data.pid = csa.pid "
					 " AND csa.query_start = timestamp LIMIT 1)"
					 " SELECT pg_cancel_backend(global_pid) FROM matching_backends;");

	bool isNull = true;
	bool readOnly = true;
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
								&isNull);

	/* Remove the create index request from Index queue as well. */
	RemoveRequestFromIndexQueue(indexId, CREATE_INDEX_COMMAND_TYPE);
	elog(LOG, "Clean up of existing index build request for %d is completed.",
		 indexId);
}
