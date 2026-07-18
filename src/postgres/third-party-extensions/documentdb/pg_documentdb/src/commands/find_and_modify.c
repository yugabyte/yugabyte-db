/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/find_and_modify.c
 *
 * Implementation of findAndModify command.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <storage/lockdefs.h>
#include <utils/builtins.h>
#include <funcapi.h>

#include "io/bson_core.h"
#include "update/bson_update.h"
#include "commands/commands_common.h"
#include "commands/delete.h"
#include "commands/insert.h"
#include "utils/documentdb_errors.h"
#include "commands/parse_error.h"
#include "commands/update.h"
#include "metadata/collection.h"
#include "query/query_operator.h"
#include "sharding/sharding.h"
#include "utils/feature_counter.h"
#include "utils/version_utils.h"
#include "schema_validation/schema_validation.h"
#include "operators/bson_expression.h"

/* Represents bson message passed to a findAndModify command */
typedef struct
{
	/* "findAndModify" field */
	char *collectionName;

	/* "query" field */
	bson_value_t *query;

	/* "sort" field */
	bson_value_t *sort;

	/* "remove" field */
	bool remove;

	/* "update" field */
	bson_value_t *update;

	/* "arrayFilters" field */
	bson_value_t *arrayFilters;

	/*
	 * "new" field
	 *
	 * True, if new document is requested
	 * False, if old / deleted document is requested
	 */
	bool returnNewDocument;

	/* "fields" field */
	bson_value_t *returnFields;

	/* "upsert" field */
	bool upsert;

	/* "bypassDocumentValidation" field */
	bool bypassDocumentValidation;

	/* parsed variable spec */
	bson_value_t variableSpec;
} FindAndModifySpec;


/* Represents bson response that needs to be returned for a findAndModify command */
typedef struct
{
	/*
	 * If set to true, then nested "lastErrorObject" document should not have
	 * "updatedExisting" and "upsertedObjectId" fields.
	 */
	bool isUpdateCommand;

	/* "lastErrorObject" field */
	struct
	{
		/* "n" field */
		unsigned int n;

		/*
		 * "updatedExisting" field
		 *
		 * When n is greater than 0; upsertedObjectId must be NULL if
		 * updatedExisting is true, and non-NULL otherwise.
		 *
		 * Obviously; when n is equal to 0, updatedExisting must be NULL and
		 * updatedExisting must be false.
		 */
		bool updatedExisting;

		/*
		 * "upserted" field
		 */
		pgbson *upsertedObjectId;
	} lastErrorObject;

	/* "value" field */
	pgbson *value;

	/* "ok" field */
	double ok;
} FindAndModifyResult;


/* findAndModify specific not-implemented options */
const char *const NotImplementedOptions[] = {
	"hint"
};


PG_FUNCTION_INFO_V1(command_find_and_modify);


static FindAndModifySpec ParseFindAndModifyMessage(pgbson *message);
static FindAndModifyResult ProcessFindAndModifySpec(MongoCollection *collection,
													FindAndModifySpec *spec,
													text *transactionId);
static pgbson * BuildResponseMessage(FindAndModifyResult *result);

extern bool SkipFailOnCollation;
extern bool EnableBypassDocumentValidation;
extern bool EnableSchemaValidation;
extern bool EnableVariablesSupportForWriteCommands;

/*
 * command_find_and_modify implements findAndModify command.
 */
Datum
command_find_and_modify(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("The parameter p_database_name must not be NULL")));
	}
	Datum databaseNameDatum = PG_GETARG_DATUM(0);

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("p_message cannot be NULL")));
	}
	pgbson *message = PgbsonDeduplicateFields(PG_GETARG_PGBSON(1));

	text *transactionId = !PG_ARGISNULL(2) ? PG_GETARG_TEXT_P(2) : NULL;

	ReportFeatureUsage(FEATURE_COMMAND_FINDANDMODIFY);

	/* fetch TupleDesc for return value, not interested in resultTypeId */
	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc;
	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	ThrowIfServerOrTransactionReadOnly();
	FindAndModifySpec spec = ParseFindAndModifyMessage(message);

	Datum collectionNameDatum = CStringGetTextDatum(spec.collectionName);
	MongoCollection *collection = GetMongoCollectionByNameDatum(databaseNameDatum,
																collectionNameDatum,
																RowExclusiveLock);
	Datum values[2];
	bool isNulls[2] = { false, false };
	HeapTuple resultTuple;

	if (collection == NULL)
	{
		ValidateCollectionNameForUnauthorizedSystemNs(spec.collectionName,
													  databaseNameDatum);

		if (spec.upsert)
		{
			/*
			 * Upsert on a non-existent collection creates the collection (or
			 * races for creating it in our implementation).
			 */
			collection = CreateCollectionForInsert(databaseNameDatum,
												   collectionNameDatum);
		}
		else
		{
			/*
			 * findAndModify on non-existent collection without upsert is a
			 * noop, but we still need to report errors due to invalid query
			 * / update documents.
			 */
			ValidateQueryDocumentValue(spec.query);

			if (!spec.remove)
			{
				const bson_value_t *variableSpec = NULL;
				ValidateUpdateDocument(spec.update, spec.query, spec.arrayFilters,
									   variableSpec);
			}

			FindAndModifyResult result = {
				.isUpdateCommand = !spec.remove,
				.lastErrorObject = {
					.n = 0,
					.updatedExisting = false,
					.upsertedObjectId = NULL
				},
				.value = NULL,
				.ok = true
			};

			values[0] = PointerGetDatum(BuildResponseMessage(&result));
			values[1] = BoolGetDatum(result.ok);
			resultTuple = heap_form_tuple(resultTupDesc, values, isNulls);
			PG_RETURN_DATUM(HeapTupleGetDatum(resultTuple));
		}
	}

	FindAndModifyResult result = ProcessFindAndModifySpec(collection, &spec,
														  transactionId);

	values[0] = PointerGetDatum(BuildResponseMessage(&result));
	values[1] = BoolGetDatum(result.ok);
	resultTuple = heap_form_tuple(resultTupDesc, values, isNulls);
	PG_RETURN_DATUM(HeapTupleGetDatum(resultTuple));
}


/*
 * ParseFindAndModifyMessage returns a FindAndModifySpec by parsing bson
 * message passed to a findAndModify command.
 */
static FindAndModifySpec
ParseFindAndModifyMessage(pgbson *message)
{
	bson_value_t let = { 0 };
	FindAndModifySpec spec = { 0 };
	spec.bypassDocumentValidation = false;
	bool applyVariableSpec = EnableVariablesSupportForWriteCommands &&
							 IsClusterVersionAtleast(DocDB_V0, 106, 0);

	bson_iter_t messageIter;
	PgbsonInitIterator(message, &messageIter);
	while (bson_iter_next(&messageIter))
	{
		const char *key = bson_iter_key(&messageIter);

		bool knownField = true;

		if (strcmp(key, "findAndModify") == 0 ||
			strcmp(key, "findandmodify") == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(&messageIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Collection name contains an invalid data type %s",
									   BsonIterTypeName(&messageIter))));
			}

			spec.collectionName = bson_iter_dup_utf8(&messageIter, NULL);
			if (strlen(spec.collectionName) == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
								errmsg(
									"An invalid and empty namespace has been specified")));
			}
		}
		else if (strcmp(key, "query") == 0)
		{
			if (EnsureTopLevelFieldTypeNullOk("findAndModify.query", &messageIter,
											  BSON_TYPE_DOCUMENT))
			{
				spec.query = CreateBsonValueCopy(bson_iter_value(&messageIter));
			}
		}
		else if (strcmp(key, "sort") == 0)
		{
			if (EnsureTopLevelFieldTypeNullOk("findAndModify.sort", &messageIter,
											  BSON_TYPE_DOCUMENT))
			{
				spec.sort = CreateBsonValueCopy(bson_iter_value(&messageIter));
			}
		}
		else if (strcmp(key, "remove") == 0)
		{
			if (EnsureTopLevelFieldIsBooleanLikeNullOk("findAndModify.remove",
													   &messageIter))
			{
				spec.remove = BsonValueAsDouble(bson_iter_value(&messageIter)) != 0;
			}
		}
		else if (strcmp(key, "update") == 0)
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&messageIter) &&
				!BSON_ITER_HOLDS_ARRAY(&messageIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("Update argument must be either an "
									   "object or an array")));
			}

			/* we keep update documents in projected form to preserve the type */
			spec.update = CreateBsonValueCopy(bson_iter_value(&messageIter));
		}
		else if (strcmp(key, "new") == 0)
		{
			if (EnsureTopLevelFieldIsBooleanLikeNullOk("findAndModify.new", &messageIter))
			{
				spec.returnNewDocument =
					BsonValueAsDouble(bson_iter_value(&messageIter)) != 0;
			}
		}
		else if (strcmp(key, "fields") == 0)
		{
			if (EnsureTopLevelFieldTypeNullOk("findAndModify.fields", &messageIter,
											  BSON_TYPE_DOCUMENT) &&
				!IsBsonValueEmptyDocument(bson_iter_value(&messageIter)))
			{
				spec.returnFields = CreateBsonValueCopy(bson_iter_value(&messageIter));
			}
		}
		else if (strcmp(key, "upsert") == 0)
		{
			if (EnsureTopLevelFieldIsBooleanLikeNullOk("findAndModify.upsert",
													   &messageIter))
			{
				spec.upsert = BsonValueAsDouble(bson_iter_value(&messageIter)) != 0;
			}
		}
		else if (strcmp(key, "arrayFilters") == 0)
		{
			if (BSON_ITER_HOLDS_NULL(&messageIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg(
									"Parameter provided is invalid; an object (arrayFilters) was expected.")));
			}

			EnsureTopLevelFieldType("findAndModify.arrayFields", &messageIter,
									BSON_TYPE_ARRAY);

			/* we keep arrayFilters in projected form to preserve the type */
			spec.arrayFilters = CreateBsonValueCopy(bson_iter_value(&messageIter));
		}
		else if (strcmp(key, "collation") == 0)
		{
			ReportFeatureUsage(FEATURE_COLLATION);

			if (!SkipFailOnCollation)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
								errmsg(
									"findAndModify.collation is not implemented yet")));
			}
		}
		else if (strcmp(key, "maxTimeMS") == 0)
		{
			EnsureTopLevelFieldIsNumberLike("findAndModify.maxTimeMS", bson_iter_value(
												&messageIter));
			SetExplicitStatementTimeout(BsonValueAsInt32(bson_iter_value(&messageIter)));
		}
		else if (EnableSchemaValidation && EnableBypassDocumentValidation &&
				 strcmp(key, "bypassDocumentValidation") == 0)
		{
			EnsureTopLevelFieldType("findAndModify.bypassDocumentValidation",
									&messageIter,
									BSON_TYPE_BOOL);

			spec.bypassDocumentValidation = bson_iter_bool(&messageIter);
		}
		else if (strcmp(key, "let") == 0)
		{
			ReportFeatureUsage(FEATURE_LET_TOP_LEVEL);
			if (applyVariableSpec)
			{
				EnsureTopLevelFieldType("findAndModify.let", &messageIter,
										BSON_TYPE_DOCUMENT);

				let = *bson_iter_value(&messageIter);
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
								errmsg("findAndModify.let is not yet supported")));
			}
		}
		else
		{
			knownField = false;
		}

		if (knownField)
		{
			continue;
		}

		/*
		 * XXX: Silently ignore so that clients don't break:
		 *  - writeConcern
		 *  - comment
		 *	- bypassDocumentValidation
		 *  - collation
		 *  - maxTimeMS
		 */
		if (IsCommonSpecIgnoredField(key))
		{
			ereport(DEBUG1, (errmsg("findAndModify.%s is not implemented yet", key)));

			continue;
		}

		/* XXX: But we don't silently ignore the following */
		for (long unsigned int i = 0; i < lengthof(NotImplementedOptions); i++)
		{
			const char *notImplementedOption = NotImplementedOptions[i];
			if (strcmp(key, notImplementedOption) == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
								errmsg("findAndModify.%s is not implemented yet",
									   notImplementedOption)));
			}
		}

		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"The BSON field 'findAndModify.%s' is not recognized as a valid field.",
							key)));
	}

	if (spec.collectionName == NULL)
	{
		ThrowTopLevelMissingFieldError("findAndModify.findAndModify");
	}

	if (spec.remove)
	{
		if (spec.update != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"It is not possible to specify both an update action and simultaneously set  "
								"remove=true")));
		}

		if (spec.upsert)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("It is not allowed to set both upsert=true "
								   "and remove=true simultaneously")));
		}

		if (spec.returnNewDocument)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"It is not allowed to set both new=true and remove=true simultaneously, as the 'remove' always returns the document that was deleted.")));
		}
	}
	else
	{
		if (spec.update == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("You must specify either an update action "
								   "or set remove=true for proper execution")));
		}
	}

	if (spec.query == NULL)
	{
		pgbson *emptyQuery = PgbsonInitEmpty();
		spec.query = palloc(sizeof(bson_value_t));
		*spec.query = ConvertPgbsonToBsonValue(emptyQuery);
	}

	if (applyVariableSpec)
	{
		bool isWriteCommand = true;
		TimeSystemVariables *timeSysVars = NULL;
		pgbson *parsedVariables = ParseAndGetTopLevelVariableSpec(&let, timeSysVars,
																  isWriteCommand);
		spec.variableSpec = ConvertPgbsonToBsonValue(parsedVariables);
	}

	return spec;
}


/*
 * ProcessFindAndModifySpec performs delete/update operation on given
 * collection based on given FindAndModifySpec.
 */
static FindAndModifyResult
ProcessFindAndModifySpec(MongoCollection *collection, FindAndModifySpec *spec,
						 text *transactionId)
{
	int64 shardKeyHash = 0;
	bool shardKeyValueCollationAware = false;
	bool hasShardKeyValueFilter =
		ComputeShardKeyHashForQueryValue(collection->shardKey,
										 collection->
										 collectionId,
										 spec->query,
										 &shardKeyHash,
										 &shardKeyValueCollationAware);

	if (!hasShardKeyValueFilter)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_SHARDKEYNOTFOUND),
						errmsg("Query for sharded findAndModify must "
							   "contain the shard key")));
	}

	if (spec->remove)
	{
		DeleteOneParams deleteOneParams = {
			.query = spec->query,
			.returnFields = spec->returnFields,
			.returnDeletedDocument = true,
			.sort = spec->sort,
			.variableSpec = &spec->variableSpec
		};

		DeleteOneResult deleteOneResult = { 0 };
		bool forceInlineWrites = false;
		CallDeleteOne(collection, &deleteOneParams, shardKeyHash, transactionId,
					  forceInlineWrites, &deleteOneResult);

		if (deleteOneResult.isRowDeleted &&
			deleteOneResult.resultDeletedDocument == NULL)
		{
			ereport(ERROR, (errmsg(
								"Failed to return the document that was previously deleted")));
		}

		FindAndModifyResult result = {
			.ok = true,
			.value = deleteOneResult.resultDeletedDocument,
			.isUpdateCommand = false,
			.lastErrorObject = {
				.n = deleteOneResult.isRowDeleted ? 1 : 0,

				/* will be ignored since isUpdateCommand=false */
				.updatedExisting = false,
				.upsertedObjectId = NULL
			}
		};

		return result;
	}
	else
	{
		UpdateOneParams updateOneParams = {
			.arrayFilters = spec->arrayFilters,
			.isUpsert = spec->upsert,
			.query = spec->query,
			.returnDocument = spec->returnNewDocument ? UPDATE_RETURNS_NEW :
							  UPDATE_RETURNS_OLD,
			.returnFields = spec->returnFields,
			.sort = spec->sort,
			.update = spec->update,
			.variableSpec = &spec->variableSpec
		};

		UpdateOneResult updateOneResult = { 0 };
		bool forceInlineWrites = false;

		ExprEvalState *evalState = NULL;
		if (CheckSchemaValidationEnabled(collection, spec->bypassDocumentValidation))
		{
			evalState = PrepareForSchemaValidation(collection->schemaValidator.validator,
												   CurrentMemoryContext);
		}

		UpdateOne(collection, &updateOneParams, shardKeyHash, transactionId,
				  &updateOneResult, forceInlineWrites, evalState);

		bool performedUpdateOrUpsert = updateOneResult.isRowUpdated ||
									   updateOneResult.upsertedObjectId != NULL;
		if (updateOneResult.isRetry)
		{
			/*
			 * Cannot verify whether UpdateOne should have returned result
			 * document if it's a retry since the value of
			 * UpdateOneParams.returnDocument is ignored in that case.
			 */
		}
		else if (updateOneResult.resultDocument == NULL)
		{
			if (updateOneParams.returnDocument == UPDATE_RETURNS_NEW &&
				performedUpdateOrUpsert)
			{
				ereport(ERROR, (errmsg("couldn't return new document")));
			}

			if (updateOneParams.returnDocument == UPDATE_RETURNS_OLD &&
				updateOneResult.isRowUpdated)
			{
				ereport(ERROR, (errmsg("couldn't return old document")));
			}
		}

		FindAndModifyResult result = {
			.ok = true,
			.value = updateOneResult.resultDocument,
			.isUpdateCommand = true,
			.lastErrorObject = {
				.n = performedUpdateOrUpsert ? 1 : 0,
				.updatedExisting = updateOneResult.isRowUpdated,
				.upsertedObjectId = updateOneResult.upsertedObjectId
			}
		};

		FreeExprEvalState(evalState, CurrentMemoryContext);
		return result;
	}
}


/*
 * BuildResponseMessage returns a bson object that can be sent to the client
 * based on given FindAndModifyResult.
 */
static pgbson *
BuildResponseMessage(FindAndModifyResult *result)
{
	pgbson_writer lastErrorObjectWriter;
	PgbsonWriterInit(&lastErrorObjectWriter);

	PgbsonWriterAppendInt32(&lastErrorObjectWriter, "n", strlen("n"),
							result->lastErrorObject.n);

	if (result->isUpdateCommand)
	{
		PgbsonWriterAppendBool(&lastErrorObjectWriter, "updatedExisting",
							   strlen("updatedExisting"),
							   result->lastErrorObject.updatedExisting);

		if (result->lastErrorObject.upsertedObjectId != NULL)
		{
			pgbsonelement idElement;
			PgbsonToSinglePgbsonElement(result->lastErrorObject.upsertedObjectId,
										&idElement);
			PgbsonWriterAppendValue(&lastErrorObjectWriter, "upserted",
									strlen("upserted"),
									&idElement.bsonValue);
		}
	}

	pgbson_writer resultWriter;
	PgbsonWriterInit(&resultWriter);

	PgbsonWriterAppendDocument(&resultWriter, "lastErrorObject",
							   strlen("lastErrorObject"),
							   PgbsonWriterGetPgbson(&lastErrorObjectWriter));
	if (result->value == NULL)
	{
		PgbsonWriterAppendNull(&resultWriter, "value", strlen("value"));
	}
	else
	{
		PgbsonWriterAppendDocument(&resultWriter, "value", strlen("value"),
								   result->value);
	}

	PgbsonWriterAppendDouble(&resultWriter, "ok", strlen("ok"), result->ok);

	return PgbsonWriterGetPgbson(&resultWriter);
}
