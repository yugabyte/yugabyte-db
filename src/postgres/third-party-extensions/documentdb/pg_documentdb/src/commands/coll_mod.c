/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/coll_mod.c
 *
 * Implementation of the collMod command.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <executor/spi.h>
#include <fmgr.h>
#include <funcapi.h>
#include <utils/builtins.h>
#include <utils/snapmgr.h>

#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "utils/documentdb_errors.h"
#include "metadata/collection.h"
#include "api_hooks.h"
#include "metadata/index.h"
#include "metadata/metadata_cache.h"
#include "utils/guc_utils.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/*
 * Index specification option available for collMod till 5.0
 */
typedef struct
{
	pgbson *keyPattern;
	char *name;
	bool hidden;
	int expireAfterSeconds;
} CollModIndexOptions;

/*
 * CollMod Database command specification options
 */
typedef struct
{
	const char *collectionName;

	/* Index update options */
	CollModIndexOptions index;

	/* A view definition it is a view */
	ViewDefinition viewDefinition;

	/* The name of the collection to colocate this collection with */
	bson_value_t colocationOptions;

	/* The validator for the collection */
	bson_value_t validator;

	/* The validation level for the collection */
	char *validationLevel;

	/* The validation action for the collection */
	char *validationAction;

	/* TODO: Add more options when they are supported e.g.: Validators etc */
} CollModOptions;

typedef enum CollModSpecFlags
{
	HAS_NO_OPTIONS = 0,

	/* Index option specific flags to identify if options is provided */
	HAS_INDEX_OPTION = 1 << 0,                          /* Set if "index" is set */
	HAS_INDEX_OPTION_NAME = 1 << 1,                     /* Set if "index.name" is set */
	HAS_INDEX_OPTION_KEYPATTERN = 1 << 2,               /* Set if "index.keyPattern" is set */
	HAS_INDEX_OPTION_HIDDEN = 1 << 3,                   /* Set if "index.hidden" is set */
	HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS = 1 << 4,     /* Set if "index.expireAfterSeconds" is set */

	/* Views update */
	HAS_VIEW_OPTION = 1 << 5,

	HAS_COLOCATION = 1 << 6,

	/* validation update */
	HAS_VALIDATION_OPTION = 1 << 7,

	/* TODO: More OPTIONS to follow */
} CollModSpecFlags;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static CollModSpecFlags ParseSpecSetCollModOptions(const pgbson *collModSpec,
												   CollModOptions *collModOptions);
static void ParseIndexSpecSetCollModOptions(bson_iter_t *indexSpecIter,
											CollModIndexOptions *collModIndexOptions,
											CollModSpecFlags *specFlags);
static void ModifyIndexSpecsInCollection(const MongoCollection *collection,
										 const CollModIndexOptions *indexOption,
										 const CollModSpecFlags *specFlags,
										 pgbson_writer *writer);
static void ModifyViewDefinition(Datum databaseDatum,
								 const MongoCollection *collection,
								 const ViewDefinition *viewDefinition,
								 pgbson_writer *writer);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(command_coll_mod);

/*
 * command_coll_mod implements the functionality of collMod Database command
 * dbcommand/collMod.
 */
Datum
command_coll_mod(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("db name cannot be NULL")));
	}

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("collection name cannot be NULL")));
	}

	if (PG_ARGISNULL(2))
	{
		ereport(ERROR, (errmsg("collMod spec cannot be NULL")));
	}
	pgbson *collModSpec = PG_GETARG_PGBSON(2);

	ReportFeatureUsage(FEATURE_COMMAND_COLLMOD);

	/*
	 * TODO: Restrict collMod command access based on RBAC when it is available
	 */

	/*
	 * Get the mongo collection with the right set of locks for coll_mod
	 * Native mongo gets an exclusive lock on the complete database of the collection
	 * (which means all the other collection as well because these can be part of the modification)
	 *
	 * We currently lock the collection data table only because none of the other option which can potentially
	 * refer other collections are supported right now . e.g. viewOn, pipelines, validators etc
	 */
	Datum databaseDatum = PG_GETARG_DATUM(0);

	/* Validate the collMod options received because GW only checks for valid collection name */
	CollModOptions collModOptions = { 0 };
	CollModSpecFlags specFlags = ParseSpecSetCollModOptions(collModSpec,
															&collModOptions);

	if (collModOptions.collectionName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE), (errmsg(
																		"collMod must be specified"))));
	}

	if (!PG_ARGISNULL(1))
	{
		const char *collectionName = TextDatumGetCString(PG_GETARG_DATUM(1));

		if (strcmp(collectionName, collModOptions.collectionName) != 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"Collection name specified in the top level must match that in the spec")));
		}
	}

	MongoCollection *collection =
		GetMongoCollectionOrViewByNameDatum(databaseDatum,
											CStringGetTextDatum(
												collModOptions.collectionName),
											AccessExclusiveLock);

	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_NAMESPACENOTFOUND),
						errmsg("ns does not exist")));
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt32(&writer, "ok", 2, 1);

	if (specFlags == HAS_NO_OPTIONS)
	{
		/* There are no operations requested, no-op */
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
	}

	if (specFlags & HAS_VIEW_OPTION)
	{
		ReportFeatureUsage(FEATURE_COMMAND_COLLMOD_VIEW);
		ModifyViewDefinition(databaseDatum, collection, &collModOptions.viewDefinition,
							 &writer);
	}
	else if (collection->viewDefinition != NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTEDONVIEW),
						errmsg("Namespace %s.%s is a view, not a collection",
							   collection->name.databaseName,
							   collection->name.collectionName)));
	}

	if (specFlags & HAS_INDEX_OPTION)
	{
		/* Index related modification requested */
		ModifyIndexSpecsInCollection(collection, &collModOptions.index,
									 &specFlags, &writer);
	}

	if (specFlags & HAS_COLOCATION)
	{
		ReportFeatureUsage(FEATURE_COMMAND_COLLMOD_COLOCATION);
		if (collection->viewDefinition != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
							errmsg("Cannot specify colocation on a view")));
		}

		HandleColocation(collection, &collModOptions.colocationOptions);
	}

	if (specFlags & HAS_VALIDATION_OPTION)
	{
		ReportFeatureUsage(FEATURE_COMMAND_COLLMOD_VALIDATION);

		/* if validationAction/validationLevel of collection is empty, it should be updated with customized or default value */
		if (collection->schemaValidator.validationAction == ValidationAction_Invalid)
		{
			collModOptions.validationAction = collModOptions.validationAction == NULL ?
											  "error" : collModOptions.validationAction;
		}
		if (collection->schemaValidator.validationLevel == ValidationLevel_Invalid)
		{
			collModOptions.validationLevel = collModOptions.validationLevel == NULL ?
											 "strict" : collModOptions.validationLevel;
		}

		UpsertSchemaValidation(databaseDatum, CStringGetTextDatum(
								   collModOptions.collectionName),
							   &collModOptions.validator,
							   collModOptions.validationLevel,
							   collModOptions.validationAction);
	}

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * Parses the collMod Options received from GW, sets the option in `CollModOptions`
 * and also returns the `CollModSpecFlags` to represent which options were provided
 */
static CollModSpecFlags
ParseSpecSetCollModOptions(const pgbson *collModSpec,
						   CollModOptions *collModOptions)
{
	Assert(collModSpec != NULL && collModOptions != NULL);

	CollModSpecFlags specFlags = HAS_NO_OPTIONS;
	bool hasSchemaValidation = false;

	bson_iter_t iter;
	PgbsonInitIterator(collModSpec, &iter);
	while (bson_iter_next(&iter))
	{
		const char *key = bson_iter_key(&iter);
		const bson_value_t *value = bson_iter_value(&iter);
		if (strcmp(key, "collMod") == 0)
		{
			EnsureTopLevelFieldType("collMod.collMod", &iter, BSON_TYPE_UTF8);
			collModOptions->collectionName = bson_iter_utf8(&iter, NULL);
		}
		else if (strcmp(key, "index") == 0)
		{
			EnsureTopLevelFieldType("collMod.index", &iter, BSON_TYPE_DOCUMENT);
			bson_iter_t indexSpecIter;
			bson_iter_recurse(&iter, &indexSpecIter);
			ParseIndexSpecSetCollModOptions(&indexSpecIter, &(collModOptions->index),
											&specFlags);
			specFlags |= HAS_INDEX_OPTION;
		}
		else if (strcmp(key, "viewOn") == 0)
		{
			EnsureTopLevelFieldType("collMod.viewOn", &iter, BSON_TYPE_UTF8);
			specFlags |= HAS_VIEW_OPTION;
			collModOptions->viewDefinition.viewSource = pnstrdup(value->value.v_utf8.str,
																 value->value.v_utf8.len);
		}
		else if (strcmp(key, "pipeline") == 0)
		{
			EnsureTopLevelFieldType("collMod.pipeline", &iter, BSON_TYPE_ARRAY);
			collModOptions->viewDefinition.pipeline = *value;
		}
		else if (strcmp(key, "colocation") == 0)
		{
			EnsureTopLevelFieldType("collMod.colocation", &iter, BSON_TYPE_DOCUMENT);
			collModOptions->colocationOptions = *value;
			specFlags |= HAS_COLOCATION;
		}
		else if (strcmp(key, "validator") == 0)
		{
			const bson_value_t *validator = ParseAndGetValidatorSpec(&iter,
																	 "collMod.validator",
																	 &hasSchemaValidation);
			if (validator == NULL)
			{
				collModOptions->validator.value_type = BSON_TYPE_EOD;
			}
			else
			{
				collModOptions->validator = *validator;
			}
		}
		else if (strcmp(key, "validationLevel") == 0)
		{
			collModOptions->validationLevel = ParseAndGetValidationLevelOption(&iter,
																			   "collMod.validationLevel",
																			   &
																			   hasSchemaValidation);
		}
		else if (strcmp(key, "validationAction") == 0)
		{
			collModOptions->validationAction = ParseAndGetValidationActionOption(&iter,
																				 "collMod.validationAction",
																				 &
																				 hasSchemaValidation);
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			/*
			 *  Silently ignore now, so that clients don't break
			 * TODO: implement "validationAction","validationLevel",
			 * "validator","viewOn", "pipeline", "expireAfterSeconds"
			 */
			elog(DEBUG1, "Unrecognized command field: collMod.%s", key);
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg("BSON field 'collMod.%s' is an unknown field.", key)));
		}
	}

	if (collModOptions->viewDefinition.pipeline.value_type != BSON_TYPE_EOD &&
		specFlags != HAS_VIEW_OPTION)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("collmod.pipeline requires collmod.viewOn")));
	}

	if (hasSchemaValidation)
	{
		specFlags |= HAS_VALIDATION_OPTION;
	}

	return specFlags;
}


/*
 * This method only parses the index options for a collmod command
 */
static void
ParseIndexSpecSetCollModOptions(bson_iter_t *indexSpecIter,
								CollModIndexOptions *collModIndexOptions,
								CollModSpecFlags *specFlags)
{
	Assert(indexSpecIter != NULL && collModIndexOptions != NULL);
	while (bson_iter_next(indexSpecIter))
	{
		const char *key = bson_iter_key(indexSpecIter);
		const bson_value_t *value = bson_iter_value(indexSpecIter);
		if (strcmp(key, "keyPattern") == 0)
		{
			EnsureTopLevelFieldType("collMod.index.keyPattern", indexSpecIter,
									BSON_TYPE_DOCUMENT);
			if (*specFlags & HAS_INDEX_OPTION_NAME)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
								errmsg("Cannot specify both key pattern and name.")));
			}
			collModIndexOptions->keyPattern = PgbsonInitFromDocumentBsonValue(value);
			*specFlags |= HAS_INDEX_OPTION_KEYPATTERN;
		}
		else if (strcmp(key, "name") == 0)
		{
			EnsureTopLevelFieldType("collMod.index.name", indexSpecIter, BSON_TYPE_UTF8);
			if (*specFlags & HAS_INDEX_OPTION_KEYPATTERN)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
								errmsg("Cannot specify both key pattern and name.")));
			}
			collModIndexOptions->name = palloc(value->value.v_utf8.len + 1);
			strcpy(collModIndexOptions->name, value->value.v_utf8.str);
			*specFlags |= HAS_INDEX_OPTION_NAME;
		}
		else if (strcmp(key, "hidden") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg("'collMod.index.hidden' is not supported yet.")));
		}
		else if (strcmp(key, "expireAfterSeconds") == 0)
		{
			if (!BsonValueIsNumber(value))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"BSON field 'collMod.index.expireAfterSeconds' is the wrong type '%s', "
									"expected types '[long, int, decimal, double']",
									BsonTypeName(value->value_type))));
			}
			int64 expireAfterSeconds = BsonValueAsInt64(value);
			if (expireAfterSeconds < 0)
			{
				/* this is interesting mongo db does not fail for this */
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
								errmsg(
									"BSON field 'collMod.index.expireAfterSeconds' cannot be less than 0.")));
			}
			collModIndexOptions->expireAfterSeconds = (uint64) expireAfterSeconds;
			*specFlags |= HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg("BSON field 'collMod.index.%s' is an unknown field.",
								   key)));
		}
	}

	if ((*specFlags & HAS_INDEX_OPTION_NAME) != HAS_INDEX_OPTION_NAME &&
		(*specFlags & HAS_INDEX_OPTION_KEYPATTERN) != HAS_INDEX_OPTION_KEYPATTERN)
	{
		/* If no name or key pattern then error */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("Must specify either index name or key pattern.")));
	}

	if ((*specFlags & HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS) !=
		HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS &&
		(*specFlags & HAS_INDEX_OPTION_HIDDEN) != HAS_INDEX_OPTION_HIDDEN)
	{
		/* If hidden or expireAfterSeconds is not provided then error */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("no expireAfterSeconds or hidden field")));
	}
}


/*
 * Updates the ApiCatalogSchemaName.collection_indexes metadata table with the requested updates
 */
static void
ModifyIndexSpecsInCollection(const MongoCollection *collection,
							 const CollModIndexOptions *indexOption,
							 const CollModSpecFlags *specFlags,
							 pgbson_writer *writer)
{
	StringInfo cmdStr = makeStringInfo();
	bool searchWithName = *specFlags & HAS_INDEX_OPTION_NAME;
	appendStringInfo(cmdStr,
					 "SELECT index_id, index_spec "
					 "FROM %s.collection_indexes "
					 "WHERE collection_id = $2 AND (index_spec).%s = $1;",
					 ApiCatalogSchemaName,
					 searchWithName ? "index_name" : "index_key");

	int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];

	if (searchWithName)
	{
		argTypes[0] = TEXTOID;
		argValues[0] = CStringGetTextDatum(indexOption->name);
	}
	else
	{
		argTypes[0] = BsonTypeId();
		argValues[0] = PointerGetDatum(indexOption->keyPattern);
	}

	argTypes[1] = INT8OID;
	argValues[1] = UInt64GetDatum(collection->collectionId);

	/* all args are non-null */
	char *argNulls = NULL;
	bool readOnly = true;
	int numValues = 2;
	bool isNull[2];
	Datum results[2];
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(cmdStr->data, argCount, argTypes,
												  argValues, argNulls, readOnly,
												  SPI_OK_SELECT, results, isNull,
												  numValues);
	if (isNull[0])
	{
		/* No matching index found with the criteria */
		ereport(ERROR,
				(errcode(ERRCODE_DOCUMENTDB_INDEXNOTFOUND),
				 errmsg("cannot find index %s for ns %s.%s",
						searchWithName ? indexOption->name : PgbsonToJsonForLogging(
							indexOption->keyPattern),
						collection->name.databaseName, collection->name.collectionName)));
	}

	IndexDetails indexDetails = { 0 };
	indexDetails.indexId = DatumGetInt32(results[0]);
	indexDetails.indexSpec = *DatumGetIndexSpec(results[1]);
	indexDetails.collectionId = collection->collectionId;

	BoolIndexOption oldHidden = BoolIndexOption_Undefined;
	BoolIndexOption newHidden = BoolIndexOption_Undefined;
	int oldTTL = 0, newTTL = 0;

	bool updateNeeded = false;

	if ((*specFlags & HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS) ==
		HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS)
	{
		if (indexDetails.indexSpec.indexExpireAfterSeconds == NULL)
		{
			/* 5.0 doesn't allow non-TTL index to be converted to TTL index */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
							errmsg("no expireAfterSeconds field to update")));
		}
		oldTTL = *(indexDetails.indexSpec.indexExpireAfterSeconds);
		newTTL = indexOption->expireAfterSeconds;
		if (oldTTL != newTTL)
		{
			*(indexDetails.indexSpec.indexExpireAfterSeconds) = newTTL;
			updateNeeded = true;
		}
	}

	if (!updateNeeded)
	{
		/* No Op */
		return;
	}

	StringInfo updateCmdStr = makeStringInfo();
	appendStringInfo(updateCmdStr,
					 "UPDATE %s.collection_indexes SET index_spec = $1"
					 " WHERE index_id = $2;", ApiCatalogSchemaName);
	int updateArgCount = 2;
	Oid updateArgTypes[2];
	Datum updateArgValues[2];

	updateArgTypes[0] = IndexSpecTypeId();
	updateArgValues[0] = IndexSpecGetDatum(CopyIndexSpec(&indexDetails.indexSpec));

	updateArgTypes[1] = INT8OID;
	updateArgValues[1] = Int64GetDatum(indexDetails.indexId);

	/* all args are non-null */
	char *updateArgNulls = NULL;
	bool updateIsNull = true;
	RunQueryWithCommutativeWrites(updateCmdStr->data,
								  updateArgCount,
								  updateArgTypes, updateArgValues, updateArgNulls,
								  SPI_OK_UPDATE, &updateIsNull);

	if ((*specFlags & HAS_INDEX_OPTION_HIDDEN) == HAS_INDEX_OPTION_HIDDEN)
	{
		PgbsonWriterAppendBool(writer, "hidden_old",
							   10, GetBoolFromBoolIndexOption(oldHidden));
		PgbsonWriterAppendBool(writer, "hidden_new",
							   10, GetBoolFromBoolIndexOption(newHidden));
	}

	if ((*specFlags & HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS) ==
		HAS_INDEX_OPTION_EXPIRE_AFTER_SECONDS)
	{
		PgbsonWriterAppendInt64(writer, "expireAfterSeconds_old",
								22, oldTTL);
		PgbsonWriterAppendDouble(writer, "expireAfterSeconds_new",
								 22, (double) newTTL);
	}
}


/*
 * Updates the view definition of an existing collection view
 * With the new view definition provided.
 * Validates the view definition and ensures it is valid first.
 * If it is, replaces the view definition in the target collection.
 */
static void
ModifyViewDefinition(Datum databaseDatum,
					 const MongoCollection *collection,
					 const ViewDefinition *viewDefinition,
					 pgbson_writer *writer)
{
	if (collection->viewDefinition == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg("ns %s.%s is a collection, not a view",
							   collection->name.databaseName,
							   collection->name.collectionName)));
	}

	if (viewDefinition->viewSource != NULL &&
		viewDefinition->pipeline.value_type == BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDOPTIONS),
						errmsg(
							"Must specify both 'viewOn' and 'pipeline' when modifying a view and auth is enabled")));
	}

	ValidateViewDefinition(
		databaseDatum, collection->name.collectionName, viewDefinition);

	/* View definition is valid, now update */
	pgbson *viewDefBson = CreateViewDefinition(viewDefinition);

	StringInfo query = makeStringInfo();
	appendStringInfo(query, "UPDATE %s.collections "
							" set view_definition = $3 WHERE database_name = $1 AND collection_name = $2",
					 ApiCatalogSchemaName);

	Oid argsTypes[3] = { TEXTOID, TEXTOID, BsonTypeId() };
	Datum argValues[3] = {
		databaseDatum, CStringGetTextDatum(collection->name.collectionName),
		PointerGetDatum(viewDefBson)
	};

	int nargs = 3;
	char *argNulls = NULL;
	bool isNullIgnore = false;
	RunQueryWithCommutativeWrites(query->data, nargs, argsTypes, argValues, argNulls,
								  SPI_OK_UPDATE, &isNullIgnore);
}
