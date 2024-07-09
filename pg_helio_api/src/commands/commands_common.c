/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/commands_common.c
 *
 * Implementation of a set of common methods for Mongo commands.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"

#include "access/xact.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"

#include "io/helio_bson_core.h"
#include "commands/commands_common.h"
#include "utils/mongo_errors.h"
#include "aggregation/bson_query.h"
#include "metadata/metadata_cache.h"
#include "planner/helio_planner.h"


/*
 *  This is a list of Mongo command options that are not currently supported.
 *  At runtime, we ignore these optional fields.
 *
 *  Note: Please keep this array sorted.
 */
static const char *IgnoredCommonSpecFields[] = {
	"$clusterTime",
	"$db",
	"$readPreference",
	"$sort",
	"allowDiskUse",
	"allowPartialResults",
	"autocommit",
	"awaitData",
	"batch_size",
	"bypassDocumentValidation", /* insert command */
	"collation",
	"collstats",
	"comment", /* insert, createIndex, dropIndex command */
	"commitQuorum", /* createIndex command */
	"db",
	"dbstats",
	"expireAfterSeconds",
	"flags",
	"indexDetails",
	"let", /* update, delete command */
	"lsid",
	"maxTimeMS",
	"noCursorTimeout",
	"oplogReplay",
	"options",
	"p5date",
	"pipeline",
	"projection",
	"readConcern", /* findAndModify */
	"readPreference",
	"returnKey",
	"showRecordId",
	"snapshot",
	"startTransaction",
	"stmtId", /* transactions */
	"storageEngine",
	"symbol",
	"tailable",
	"timeseries",
	"txnNumber",
	"validationAction",
	"validationLevel",
	"validator",
	"viewOn",
	"writeConcern"  /* insert, update, delete, createIndex, dropIndex command */
};

static int NumberOfIgnoredFields = sizeof(IgnoredCommonSpecFields) / sizeof(char *);

/* Forward declartion */
static int CompareStringsCaseInsensitive(const void *a, const void *b);
static pgbson * RewriteDocumentAddObjectIdCore(const bson_value_t *docValue,
											   bson_value_t *objectIdToWrite);

/*
 * FindShardKeyValueForDocumentId queries the collection for the shard key value that
 * corresponds to document ID and matches the query. If there are multiple
 * document IDs that match, it uses the smallest one.
 */
bool
FindShardKeyValueForDocumentId(MongoCollection *collection, pgbson *queryDoc,
							   bson_value_t *objectId, int64 *shardKeyValue)
{
	StringInfoData selectQuery;
	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];
	bool foundDocument = false;

	SPI_connect();
	initStringInfo(&selectQuery);
	appendStringInfo(&selectQuery,
					 "SELECT shard_key_value FROM %s.documents_" UINT64_FORMAT
					 " WHERE object_id = $1::%s"
					 " AND document OPERATOR(%s.@@) $2::%s ORDER BY object_id LIMIT 1",
					 ApiDataSchemaName, collection->collectionId,
					 FullBsonTypeName, ApiCatalogSchemaName, FullBsonTypeName);

	/* object_id column uses the projected value format */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendValue(&writer, "", 0, objectId);

	argTypes[0] = BYTEAOID;
	argValues[0] = PointerGetDatum(CastPgbsonToBytea(PgbsonWriterGetPgbson(&writer)));

	/* we use bytea because bson may not have the same OID on all nodes */
	argTypes[1] = BYTEAOID;
	argValues[1] = PointerGetDatum(CastPgbsonToBytea(queryDoc));

	char *argNulls = NULL;
	bool readOnly = false;
	long maxTupleCount = 0;

	SPI_execute_with_args(selectQuery.data, argCount, argTypes, argValues, argNulls,
						  readOnly, maxTupleCount);

	if (SPI_processed > 0)
	{
		bool isNull = false;
		int columnNumber = 1;

		*shardKeyValue = DatumGetUInt64(SPI_getbinval(SPI_tuptable->vals[0],
													  SPI_tuptable->tupdesc,
													  columnNumber, &isNull));

		foundDocument = true;
	}

	SPI_finish();

	return foundDocument;
}


/*
 * These are common fields that are in command spec documents
 * If the command doesn't handle them, these fields are currently
 * ignored. As the API surface improves, some of these can be made
 * required (e.g. startTransactionId).
 */
bool
IsCommonSpecIgnoredField(const char *fieldName)
{
	char **pItem = (char **) bsearch(&fieldName, IgnoredCommonSpecFields,
									 NumberOfIgnoredFields,
									 sizeof(char *), CompareStringsCaseInsensitive);
	return (pItem != NULL);
}


/*
 * Extracts the object_id if applicable from a query doc and returns a serialized pgbson
 * containing the object_id (top level column) value if one was extracted.
 * Returns NULL otherwise.
 */
pgbson *
GetObjectIdFilterFromQueryDocument(pgbson *queryDoc)
{
	bson_iter_t queryIterator;
	bson_value_t queryIdValue;
	bool errorOnConflict = false;
	PgbsonInitIterator(queryDoc, &queryIterator);
	if (TraverseQueryDocumentAndGetId(&queryIterator, &queryIdValue, errorOnConflict))
	{
		return BsonValueToDocumentPgbson(&queryIdValue);
	}

	return NULL;
}


static int
CompareStringsCaseInsensitive(const void *a, const void *b)
{
	return strcasecmp(*(char **) a, *(char **) b);
}


/*
 * GetWriteErrorFromErrorData checks if the error is an error we should rethrow
 * and if not, returns a WriteError with the details of the error data.
 */
WriteError *
GetWriteErrorFromErrorData(ErrorData *errorData, int writeErrorIdx)
{
	/*
	 * If the write error is because we're in a readonly state, which means we are in recovery mode
	 * when the primary node failover and we are waiting for the standby to be promoted as primary,
	 * we need to rethrow the error so that the gateway actually retries the write after some time,
	 * to see if the standby promotion is finished.
	 */
	if (errorData->sqlerrcode == ERRCODE_READ_ONLY_SQL_TRANSACTION)
	{
		ThrowErrorData(errorData);
	}

	WriteError *writeError = palloc0(sizeof(WriteError));
	if (errorData->sqlerrcode == ERRCODE_CHECK_VIOLATION)
	{
		ereport(LOG, (errmsg("Check constraint error %s", errorData->message)));
		writeError->index = writeErrorIdx;
		writeError->code = MongoDuplicateKey;
		writeError->errmsg =
			"Invalid write detected. Please validate the collection and/or shard key being written to";
	}
	else if (errorData->sqlerrcode == ERRCODE_EXCLUSION_VIOLATION ||
			 errorData->sqlerrcode == ERRCODE_UNIQUE_VIOLATION)
	{
		const char *mongoIndexName = NULL;
		bool useLibPq = true;
		if (errorData->constraint_name == NULL)
		{
			/* If the collection is on a remote node, this ends up being null. */
			StringView constraintError = CreateStringViewFromString(
				"conflicting key value violates exclusion constraint \"");
			StringView uniqueIndexError = CreateStringViewFromString(
				"duplicate key value violates unique constraint \"");
			StringView errorView = CreateStringViewFromString(errorData->message);
			if (StringViewStartsWithStringView(&errorView, &constraintError))
			{
				StringView indexNameView = StringViewSubstring(&errorView,
															   constraintError.length);
				StringView actualNameView = StringViewFindPrefix(&indexNameView, '\"');
				mongoIndexName = GetHelioIndexNameFromPostgresIndex(
					CreateStringFromStringView(&actualNameView), useLibPq);
			}
			else if (StringViewStartsWithStringView(&errorView, &uniqueIndexError))
			{
				StringView indexNameView = StringViewSubstring(&errorView,
															   uniqueIndexError.length);
				StringView actualNameView = StringViewFindPrefix(&indexNameView, '\"');
				mongoIndexName = GetHelioIndexNameFromPostgresIndex(
					CreateStringFromStringView(&actualNameView), useLibPq);
			}
		}
		else
		{
			mongoIndexName = GetHelioIndexNameFromPostgresIndex(
				errorData->constraint_name, useLibPq);
		}

		if (mongoIndexName == NULL)
		{
			mongoIndexName = "<unknown>";
		}

		char *errorMessage = psprintf(
			"Duplicate key violation on the requested collection: Index '%s'",
			mongoIndexName);
		writeError->index = writeErrorIdx;
		writeError->code = MongoDuplicateKey;
		writeError->errmsg = errorMessage;
	}
	else
	{
		writeError->index = writeErrorIdx;
		writeError->code = errorData->sqlerrcode;
		writeError->errmsg = pstrdup(errorData->message);
	}

	return writeError;
}


/*
 * Ensures that the _id field in a write document conforms to the requirements of Mongo
 * Right now this ensures that the _id is not undefined or an array or a regex pattern
 */
void
ValidateIdField(const bson_value_t *idValue)
{
	if ((idValue->value_type == BSON_TYPE_ARRAY) ||
		(idValue->value_type == BSON_TYPE_UNDEFINED) ||
		(idValue->value_type == BSON_TYPE_REGEX))
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"The '_id' value cannot be of type %s",
							BsonTypeName(idValue->value_type))));
	}

	if (idValue->value_type == BSON_TYPE_DOCUMENT)
	{
		bson_iter_t docIterator;
		BsonValueInitIterator(idValue, &docIterator);
		while (bson_iter_next(&docIterator))
		{
			const char *key = bson_iter_key(&docIterator);
			if (key[0] == '$')
			{
				ereport(ERROR, (errcode(MongoDollarPrefixedFieldName),
								errmsg("_id fields may not contain '$'-prefixed fields:"
									   " %s is not valid for storage.", key)));
			}
		}
	}
}


/*
 * RewriteDocumentValueAddObjectId ensures that the document has an _id field
 * and it is the first field in the document.
 *
 * If no _id was generated (Since it was there and the first field),
 * returns the original document as a pgbson.
 */
pgbson *
RewriteDocumentValueAddObjectId(const bson_value_t *value)
{
	bson_value_t *objectIdToWrite = NULL;
	pgbson *result = RewriteDocumentAddObjectIdCore(value, objectIdToWrite);
	if (result == NULL)
	{
		return PgbsonInitFromDocumentBsonValue(value);
	}

	return result;
}


/*
 * RewriteDocumentAddObjectId ensures that the document has an _id field
 * and it is the first field in the document.
 *
 * If no _id was generated (Since it was there and the first field),
 * returns the original document.
 */
pgbson *
RewriteDocumentAddObjectId(pgbson *document)
{
	bson_value_t *objectIdToWrite = NULL;
	bson_value_t value = ConvertPgbsonToBsonValue(document);
	pgbson *result = RewriteDocumentAddObjectIdCore(&value, objectIdToWrite);
	if (result == NULL)
	{
		return document;
	}

	return result;
}


/*
 * This function closely resembles `RewriteDocumentValueAddObjectId`
 * Additionally accepts an object ID as input, allowing it to insert the same object ID into the document if it is absent.
 */
pgbson *
RewriteDocumentWithCustomObjectId(pgbson *document,
								  pgbson *objectIdToWrite)
{
	pgbsonelement objectIdElement;
	TryGetSinglePgbsonElementFromPgbson(objectIdToWrite, &objectIdElement);

	Assert(objectIdElement.bsonValue.value_type == BSON_TYPE_OID);

	bson_value_t value = ConvertPgbsonToBsonValue(document);
	pgbson *result = RewriteDocumentAddObjectIdCore(&value, &objectIdElement.bsonValue);
	if (result == NULL)
	{
		return document;
	}

	return result;
}


/*
 * Core logic for RewriteDocumentAddObjectId.
 * Traverses the document pointed by the docValue,
 * if the _id is the first field, then returns NULL.
 * If the _id is found, rewrites it to be the first field.
 * If the _id is not found, and objectIdToWrite is not null, then use objectIdToWrite as the _id field and concatenate the remaining doc.
 * If the _id is not found, and objectIdToWrite is null, then generate one and concatenate the remaining doc.
 */
static pgbson *
RewriteDocumentAddObjectIdCore(const bson_value_t *docValue,
							   bson_value_t *objectIdToWrite)
{
	bson_iter_t it;
	BsonValueInitIterator(docValue, &it);
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bool isFirstField = true;
	bool documentHasIdField = false;
	while (bson_iter_next(&it))
	{
		StringView pathView = bson_iter_key_string_view(&it);
		if (StringViewEquals(&pathView, &IdFieldStringView))
		{
			/* Found an _id already */
			if (isFirstField)
			{
				/* If the _id is the first field, we're done */
				ValidateIdField(bson_iter_value(&it));
				return NULL;
			}

			documentHasIdField = true;
			break;
		}

		isFirstField = false;
	}

	if (documentHasIdField)
	{
		/* object_id found. extract. */
		bson_iter_t documentIterator;
		const bson_value_t *value;
		value = bson_iter_value(&it);
		ValidateIdField(value);

		/* copy to the modified document but add in _id first. */
		PgbsonWriterAppendValue(&writer, "_id", 3, value);
		BsonValueInitIterator(docValue, &documentIterator);
		while (bson_iter_next(&documentIterator))
		{
			const char *bsonKey = bson_iter_key(&documentIterator);
			int bsonKeyLen = bson_iter_key_len(&documentIterator);
			if (strcmp(bsonKey, "_id") == 0)
			{
				continue;
			}

			value = bson_iter_value(&documentIterator);
			PgbsonWriterAppendValue(&writer, bsonKey, bsonKeyLen, value);
		}
	}
	else
	{
		bson_value_t objectidValue;
		objectidValue.value_type = BSON_TYPE_OID;
		if (objectIdToWrite)
		{
			/* if objectId is passed by caller then we should write that */
			objectidValue = *objectIdToWrite;
		}
		else
		{
			/* generate new object_id and set objectid. */
			bson_oid_init(&(objectidValue.value.v_oid), NULL);
		}

		/* set the content now and add the object_id. */
		PgbsonWriterAppendValue(&writer, "_id", 3, &objectidValue);
		PgbsonWriterConcatBytes(&writer, docValue->value.v_doc.data,
								docValue->value.v_doc.data_len);
	}

	return PgbsonWriterGetPgbson(&writer);
}
