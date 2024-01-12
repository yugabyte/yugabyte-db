/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/create_collection_view.c
 *
 * Implementation of view and collection creation functions.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"

#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "nodes/pg_list.h"
#include "utils/varlena.h"
#include "utils/builtins.h"
#include "parser/parse_node.h"

#include "commands/commands_common.h"
#include "commands/parse_error.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "utils/mongo_errors.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "utils/feature_counter.h"

/*
 * The parsed/processed specification for a
 * Create command (representing a create collection
 * or view).
 */
typedef struct CreateSpec
{
	/* represents value of "create" field */
	char *name;

	bool capped;

	/* timeseries */

	/* expireAfterSeconds */

	/* clusteredIndex */

	/* changeStreamPreAndPostImages */

	/* autoIndexId */

	/* size (max_size) */

	/* max: (max_documents) */

	/* storageEngine */

	/* validator */

	/* validationLevel */

	/* validationAction */

	/* indexOptionDefaults */

	/* viewOn */
	char *viewOn;

	/* pipeline */
	bson_value_t pipeline;

	/* collation */

	/* writeConcern */

	/* encryptedFields */

	/* idIndex */
	bson_value_t idIndex;

	/* comment */
} CreateSpec;

static const StringView SystemPrefix = { .string = "system.", .length = 7 };

static CreateSpec * ParseCreateSpec(Datum databaseDatum, pgbson *createSpec);

static void ValidateCollectionOptionsEquivalent(CreateSpec *createDefinition,
												MongoCollection *collection);

static void CheckForViewCyclesAndDepth(Datum databaseDatum, const char *viewName,
									   const char *viewSource);

static void WalkPipelineForViewCycles(Datum databaseDatum, const char *viewName,
									  const bson_value_t *pipelineView);

static bool CreateView(Datum databaseDatum, const char *viewName,
					   const char *viewSource, const bson_value_t *pipeline);

PG_FUNCTION_INFO_V1(command_create_collection_view);


/*
 * command_create_collection_view represents the wire
 * protocol message create() which creates a mongo
 * collection or view.
 */
Datum
command_create_collection_view(PG_FUNCTION_ARGS)
{
	Datum databaseDatum = PG_GETARG_DATUM(0);
	pgbson *createSpec = PG_GETARG_PGBSON(1);

	CreateSpec *createDefinition = ParseCreateSpec(databaseDatum, createSpec);
	Datum createDatum = CStringGetTextDatum(createDefinition->name);
	ValidateDatabaseCollection(databaseDatum, createDatum);
	MongoCollection *collection =
		GetMongoCollectionOrViewByNameDatum(
			databaseDatum, createDatum, NoLock);

	if (collection != NULL)
	{
		/* Collection exists validate options */
		ValidateCollectionOptionsEquivalent(createDefinition, collection);
	}
	else if (createDefinition->viewOn != NULL)
	{
		Datum viewOnDatum = CStringGetTextDatum(createDefinition->viewOn);
		ValidateDatabaseCollection(databaseDatum, viewOnDatum);

		/* It's a view: create it */
		ReportFeatureUsage(FEATURE_COMMAND_CREATE_VIEW);
		CreateView(databaseDatum, createDefinition->name,
				   createDefinition->viewOn, &createDefinition->pipeline);
	}
	else
	{
		/* It's a collection: create it */
		ReportFeatureUsage(FEATURE_COMMAND_CREATE_COLLECTION);
		CreateCollection(databaseDatum, createDatum);
	}

	/* If we got here we succeeded, just return { ok: 1 } */
	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * Decomposes a view definition spec into its struct form.
 * See also CreateViewDefinition
 */
void
DecomposeViewDefinition(pgbson *viewSpec, ViewDefinition *viewDefinition)
{
	bson_iter_t viewIter;
	PgbsonInitIterator(viewSpec, &viewIter);

	while (bson_iter_next(&viewIter))
	{
		const char *key = bson_iter_key(&viewIter);

		/* Only two fields, 'viewOn', and 'pipeline'
		 * Note that these are pre-created versions of the document from
		 * CreateViewDefinition so we know that those are the only fields.
		 * Technically, we could just write it out as 'v' and 'p' but we use
		 * the full string just so that getCollection etc is easier for the mongo
		 * protocol (we can bson_concat there).
		 */
		if (key[0] == 'v')
		{
			viewDefinition->viewSource = bson_iter_utf8(&viewIter, NULL);
		}
		else
		{
			viewDefinition->pipeline = *bson_iter_value(&viewIter);
		}
	}
}


/*
 * Validates the "idIndex" field of a create() specification.
 * Today we only support { "_id": 1, name: "_id_", "v": 2 }
 */
static void
ValidateIdIndexDocument(const bson_value_t *idIndexDocument)
{
	bson_iter_t idIterator;
	BsonValueInitIterator(idIndexDocument, &idIterator);

	while (bson_iter_next(&idIterator))
	{
		const char *key = bson_iter_key(&idIterator);
		if (strcmp(key, "key") == 0)
		{
			EnsureTopLevelFieldType("create.idIndex.key", &idIterator,
									BSON_TYPE_DOCUMENT);

			bson_iter_t keyIterator;
			BsonValueInitIterator(bson_iter_value(&idIterator), &keyIterator);

			pgbsonelement idKeyElement;
			if (!TryGetSinglePgbsonElementFromBsonIterator(&keyIterator, &idKeyElement))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("The _id index cannot be a composite index")));
			}

			if (strcmp(idKeyElement.path, "_id") != 0)
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg("The _id index must be on the _id field")));
			}
		}
		else if (strcmp(key, "name") == 0)
		{
			EnsureTopLevelFieldType("create.idIndex.name", &idIterator, BSON_TYPE_UTF8);

			/* The name is ignored. */
		}
		else if (strcmp(key, "v") == 0)
		{
			/* The version is ignored. */
		}
		else if (strcmp(key, "ns") == 0)
		{
			/* The namespace is ignored. */
		}
		else
		{
			ereport(ERROR, (errcode(MongoInvalidIndexSpecificationOption),
							errmsg("BSON field 'create.idIndex.%s' is "
								   "invalid for an _id index", key)));
		}
	}
}


/*
 * ParseCreateSpec parses the wire
 * protocol message create() which creates a mongo
 * collection or view.
 */
static CreateSpec *
ParseCreateSpec(Datum databaseDatum, pgbson *createSpec)
{
	bson_iter_t createIter;
	PgbsonInitIterator(createSpec, &createIter);

	CreateSpec *spec = palloc0(sizeof(CreateSpec));
	while (bson_iter_next(&createIter))
	{
		const char *key = bson_iter_key(&createIter);
		if (strcmp(key, "create") == 0)
		{
			EnsureTopLevelFieldType("create.create", &createIter, BSON_TYPE_UTF8);

			uint32_t strLength = 0;
			spec->name = pstrdup(bson_iter_utf8(&createIter, &strLength));

			if (strlen(spec->name) != (size_t) strLength)
			{
				ereport(ERROR, (errcode(MongoInvalidNamespace),
								errmsg(
									"namespaces cannot have embedded null characters")));
			}
		}
		else if (strcmp(key, "viewOn") == 0)
		{
			if (EnsureTopLevelFieldTypeNullOkUndefinedOK("create.viewOn", &createIter,
														 BSON_TYPE_UTF8))
			{
				uint32_t strLength = 0;
				spec->viewOn = pstrdup(bson_iter_utf8(&createIter, &strLength));
				if (strlen(spec->viewOn) == 0)
				{
					ereport(ERROR, (errcode(MongoBadValue),
									errmsg("'viewOn' cannot be empty")));
				}
				else if (strlen(spec->viewOn) != (size_t) strLength)
				{
					ereport(ERROR, (errcode(MongoInvalidNamespace),
									errmsg(
										"namespaces cannot have embedded null characters")));
				}
			}
		}
		else if (strcmp(key, "pipeline") == 0)
		{
			EnsureTopLevelFieldType("create.pipeline", &createIter, BSON_TYPE_ARRAY);
			spec->pipeline = *bson_iter_value(&createIter);
		}
		else if (strcmp(key, "capped") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("create.capped", &createIter);
			if (BsonValueAsBool(bson_iter_value(&createIter)) == true)
			{
				ereport(ERROR, (errcode(MongoCommandNotSupported),
								errmsg("Capped collections not supported yet")));
			}
		}
		else if (strcmp(key, "timeseries") == 0)
		{
			EnsureTopLevelFieldType("create.timeseries", &createIter, BSON_TYPE_DOCUMENT);
			if (!IsBsonValueEmptyDocument(bson_iter_value(&createIter)))
			{
				ereport(ERROR, (errcode(MongoCommandNotSupported),
								errmsg("time series collections not supported yet")));
			}
		}
		else if (strcmp(key, "clusteredIndex") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("clusteredIndex not supported yet")));
		}
		else if (strcmp(key, "expireAfterSeconds") == 0)
		{
			/* Ignore: Timeseries or clustered collection options */
		}
		else if (strcmp(key, "changeStreamPreAndPostImages") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("changeStreamPreAndPostImages not supported yet")));
		}
		else if (strcmp(key, "autoIndexId") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("create.autoIndexId", &createIter);
			if (!bson_iter_as_bool(&createIter))
			{
				ereport(ERROR, (errcode(MongoInvalidOptions),
								errmsg("'autoIndexId' must be true")));
			}
		}
		else if (strcmp(key, "idIndex") == 0)
		{
			EnsureTopLevelFieldType("create.idIndex", &createIter, BSON_TYPE_DOCUMENT);
			spec->idIndex = *bson_iter_value(&createIter);

			if (IsBsonValueEmptyDocument(&spec->idIndex))
			{
				ereport(ERROR, (errcode(MongoFailedToParse),
								errmsg(
									"Field create.idIndex cannot be an empty document")));
			}

			/* Validate that it is of the right format */
			ValidateIdIndexDocument(&spec->idIndex);
		}
		else if (strcmp(key, "size") == 0 || strcmp(key, "max") == 0)
		{
			/* Ignore: Capped collection options */
		}
		else if (strcmp(key, "storageEngine") == 0 ||
				 strcmp(key, "indexOptionDefaults") == 0)
		{
			/* Ignore: Storage engine options */
		}
		else if (strcmp(key, "validator") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("validator not supported yet")));
		}
		else if (strcmp(key, "validationLevel") == 0 ||
				 strcmp(key, "validationAction") == 0)
		{
			/* Ignore: Validator options */
		}
		else if (strcmp(key, "writeConcern") == 0)
		{
			/* Ignore: Should we fail here? */
		}
		else if (strcmp(key, "encryptedFields") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("encryptedFields not supported yet")));
		}
		else if (strcmp(key, "comment") == 0)
		{
			/* ignore */
		}
		else if (!IsCommonSpecIgnoredField(key))
		{
			ereport(ERROR, (errcode(MongoUnknownBsonField),
							errmsg("BSON field 'create.%s' is an "
								   "unknown field", key)));
		}
	}

	/* Validate */
	if (spec->name == NULL || strlen(spec->name) == 0)
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg("Invalid namespace specified '%s.'",
							   TextDatumGetCString(databaseDatum))));
	}

	if (spec->viewOn == NULL && spec->pipeline.value_type != BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoInvalidOptions),
						errmsg("'pipeline' requires 'viewOn' to also be specified")));
	}

	if (spec->viewOn != NULL && spec->idIndex.value_type != BSON_TYPE_EOD)
	{
		ereport(ERROR, (errcode(MongoInvalidOptions),
						errmsg("'viewOn' and 'idIndex' cannot both be specified")));
	}

	return spec;
}


/*
 * Checks the pipeline from a 'view' definition for any
 * unsupported pipeline stages ($out and $merge).
 */
static void
CheckUnsupportedViewPipelineStages(const bson_value_t *pipeline)
{
	bson_iter_t pipelineIter;
	BsonValueInitIterator(pipeline, &pipelineIter);

	while (bson_iter_next(&pipelineIter))
	{
		bson_iter_t documentIterator;
		if (!BSON_ITER_HOLDS_DOCUMENT(&pipelineIter) ||
			!bson_iter_recurse(&pipelineIter, &documentIterator))
		{
			ereport(ERROR, (errcode(MongoTypeMismatch),
							errmsg(
								"Each element of the 'pipeline' array must be an object")));
		}

		pgbsonelement stageElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&documentIterator, &stageElement))
		{
			/* Will be validated after */
			continue;
		}

		if (strcmp(stageElement.path, "$out") == 0 ||
			strcmp(stageElement.path, "$merge") == 0)
		{
			ereport(ERROR, (errcode(MongoOptionNotSupportedOnView),
							errmsg(
								"The aggregation stage %s of the pipeline cannot be used "
								"in the view definition because it writes to disk",
								stageElement.path)));
		}
	}
}


/*
 * Validates whether the pipeline is valid for a createView.
 */
static void
ValidatePipelineForCreateView(Datum databaseDatum, const char *viewName,
							  const bson_value_t *pipeline)
{
	MemoryContext savedMemoryContext = CurrentMemoryContext;
	PG_TRY();
	{
		StringView viewStringView = CreateStringViewFromString(viewName);
		CheckUnsupportedViewPipelineStages(pipeline);
		ValidateAggregationPipeline(databaseDatum, &viewStringView,
									pipeline);
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(savedMemoryContext);
		RethrowPrependMongoError("Invalid pipeline for view caused by :: ");
	}
	PG_END_TRY();
}


/*
 * Wrapper that takes a view definition and applies all the validations
 * for the view. Checks for cycles in the views, and checks the pipeline
 * to check for sanity within the pipeline. Additionally checks for
 * unsupported stages ($out/$merge).
 */
void
ValidateViewDefinition(Datum databaseDatum, const char *viewName, const
					   ViewDefinition *definition)
{
	CheckForViewCyclesAndDepth(databaseDatum, viewName, definition->viewSource);

	if (definition->pipeline.value_type != BSON_TYPE_EOD)
	{
		ValidatePipelineForCreateView(databaseDatum, definition->viewSource,
									  &definition->pipeline);
		WalkPipelineForViewCycles(databaseDatum, viewName, &definition->pipeline);
	}
}


/*
 * Creating a view is simply registering the view as metadata in ApiCatalogSchemaName.collections.
 */
static bool
CreateView(Datum databaseDatum, const char *viewName,
		   const char *viewSource, const bson_value_t *pipeline)
{
	ViewDefinition definition = { .pipeline = *pipeline, .viewSource = viewSource };
	ValidateViewDefinition(databaseDatum, viewName, &definition);

	StringView viewNameView = CreateStringViewFromString(viewName);
	if (StringViewStartsWithStringView(&viewNameView, &SystemPrefix))
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg("Cannot create a view called %s", viewName)));
	}

	pgbson *viewDefinition = CreateViewDefinition(&definition);

	StringInfo query = makeStringInfo();
	appendStringInfo(query, "INSERT INTO %s.collections "
							" (database_name, collection_name, view_definition) VALUES "
							" ($1, $2, $3)", ApiCatalogSchemaName);

	Oid argsTypes[3] = { TEXTOID, TEXTOID, BsonTypeId() };
	Datum argValues[3] = {
		databaseDatum, CStringGetTextDatum(viewName),
		PointerGetDatum(viewDefinition)
	};

	bool readOnly = false;
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;
	SPI_connect();
	spiStatus = SPI_execute_with_args(query->data, 3, argsTypes, argValues, NULL,
									  readOnly, 1);
	SPI_finish();
	return spiStatus == SPI_OK_INSERT;
}


/*
 * Checks if an existent collection and view (or new collection) are equivalent
 * and throws the appropriate error code.
 */
static void
ValidateCollectionOptionsEquivalent(CreateSpec *createDefinition,
									MongoCollection *collection)
{
	if (collection->viewDefinition == NULL && createDefinition->viewOn != NULL)
	{
		/* We have a collection, we're trying to create a view - error */
		ereport(ERROR, (errcode(MongoNamespaceExists),
						errmsg("ns: %s.%s already exists with different options: {}",
							   collection->name.databaseName,
							   collection->name.collectionName)));
	}

	if (collection->viewDefinition != NULL && createDefinition->viewOn == NULL)
	{
		/* We have a view and trying to create a collection - error */
		ereport(ERROR, (errcode(MongoNamespaceExists),
						errmsg("ns: %s.%s already exists with different options: %s",
							   collection->name.databaseName,
							   collection->name.collectionName,
							   PgbsonToJsonForLogging(collection->viewDefinition))));
	}

	if (collection->viewDefinition != NULL && createDefinition->viewOn != NULL)
	{
		/* They are both views: Ensure that they are the same */
		ViewDefinition definition = {
			.viewSource = createDefinition->viewOn,
			.pipeline = createDefinition->pipeline
		};
		pgbson *viewDefinition = CreateViewDefinition(&definition);
		if (!PgbsonEquals(collection->viewDefinition, viewDefinition))
		{
			ereport(ERROR, (errcode(MongoNamespaceExists),
							errmsg("ns: %s.%s already exists with different options: %s",
								   collection->name.databaseName,
								   collection->name.collectionName,
								   PgbsonToJsonForLogging(collection->viewDefinition))));
		}
	}

	/* They are both collections, or both views with the same options - it's valid. */
}


/*
 * Creates a view definition serialized spec to be stored in the collections table.
 * Also see DecomposeViewDefinition
 */
pgbson *
CreateViewDefinition(const ViewDefinition *viewDefinition)
{
	pgbson_writer viewDefinitionWriter;
	PgbsonWriterInit(&viewDefinitionWriter);

	PgbsonWriterAppendUtf8(&viewDefinitionWriter, "viewOn", 6,
						   viewDefinition->viewSource);

	if (viewDefinition->pipeline.value_type != BSON_TYPE_EOD)
	{
		PgbsonWriterAppendValue(&viewDefinitionWriter, "pipeline", 8,
								&viewDefinition->pipeline);
	}

	return PgbsonWriterGetPgbson(&viewDefinitionWriter);
}


/*
 * Walks the aggregation pipeline specification and checks for cycles.
 * There's 2 stages that can refer to other tables: lookup, and facet.
 * For each asserts that no cycles end up from the view.
 */
static void
WalkPipelineForViewCycles(Datum databaseDatum, const char *viewName,
						  const bson_value_t *pipelineView)
{
	CHECK_FOR_INTERRUPTS();
	check_stack_depth();

	bson_iter_t pipelineIter;
	BsonValueInitIterator(pipelineView, &pipelineIter);
	while (bson_iter_next(&pipelineIter))
	{
		bson_iter_t documentIterator;
		if (!BSON_ITER_HOLDS_DOCUMENT(&pipelineIter) ||
			!bson_iter_recurse(&pipelineIter, &documentIterator))
		{
			ereport(ERROR, (errcode(MongoTypeMismatch),
							errmsg(
								"Each element of the 'pipeline' array must be an object")));
		}

		pgbsonelement stageElement;
		if (!TryGetSinglePgbsonElementFromBsonIterator(&documentIterator, &stageElement))
		{
			/* Will be validated after if needed */
			continue;
		}

		if (strcmp(stageElement.path, "$out") == 0 ||
			strcmp(stageElement.path, "$merge") == 0)
		{
			ereport(ERROR, (errcode(MongoLocation51047),
							errmsg(
								"The aggregation stage %s of the pipeline cannot be used "
								"in a lookup pipeline because it writes to disk",
								stageElement.path)));
		}

		if (strcmp(stageElement.path, "$lookup") == 0)
		{
			/* Validate cycles here */
			StringView collection = { 0 };
			bson_value_t pipeline = { 0 };
			LookupExtractCollectionAndPipeline(&stageElement.bsonValue, &collection,
											   &pipeline);

			CheckForViewCyclesAndDepth(databaseDatum, viewName,
									   CreateStringFromStringView(&collection));

			if (pipeline.value_type != BSON_TYPE_EOD)
			{
				WalkPipelineForViewCycles(databaseDatum, viewName,
										  &pipeline);
			}
		}
		else if (strcmp(stageElement.path, "$facet") == 0)
		{
			/* Validate cycles on nested pipeline */
			if (stageElement.bsonValue.value_type != BSON_TYPE_DOCUMENT)
			{
				continue;
			}

			bson_iter_t facetSpecIter;
			BsonValueInitIterator(&stageElement.bsonValue, &facetSpecIter);
			while (bson_iter_next(&facetSpecIter))
			{
				/* Each value is a pipeline */
				if (!BSON_ITER_HOLDS_ARRAY(&facetSpecIter))
				{
					continue;
				}

				WalkPipelineForViewCycles(databaseDatum, viewName,
										  bson_iter_value(&facetSpecIter));
			}
		}
	}
}


/*
 * Checks for parent views and collections to ensure that there is no cycle in the view
 * definition before its creation.
 */
static void
CheckForViewCyclesAndDepth(Datum databaseDatum, const char *viewName, const
						   char *viewSource)
{
	/* Ensure there is no cycle in existing views (assuming there's no cycles to begin with) */
	if (strcmp(viewName, viewSource) == 0)
	{
		const char *databaseStr = TextDatumGetCString(databaseDatum);
		ereport(ERROR, (errcode(MongoGraphContainsCycle),
						errmsg("View cycle detected: %s.%s -> %s.%s",
							   databaseStr, viewName, databaseStr, viewSource)));
	}

	List *intermediateViews = NIL;

	Datum viewSourceDatum = CStringGetTextDatum(viewSource);
	MongoCollection *collection = GetMongoCollectionOrViewByNameDatum(databaseDatum,
																	  viewSourceDatum,
																	  NoLock);

	bool cycleDetected = false;
	while (collection != NULL && !cycleDetected)
	{
		if (list_length(intermediateViews) > MAX_VIEW_DEPTH)
		{
			ereport(ERROR, (errcode(MongoViewDepthLimitExceeded),
							errmsg("View depth exceeded limit %d", MAX_VIEW_DEPTH)));
		}

		CHECK_FOR_INTERRUPTS();
		if (strcmp(collection->name.collectionName, viewName) == 0)
		{
			cycleDetected = true;
			break;
		}
		else if (collection->viewDefinition != NULL)
		{
			ViewDefinition definition = { 0 };
			DecomposeViewDefinition(collection->viewDefinition, &definition);
			intermediateViews = lappend(intermediateViews, pstrdup(
											collection->name.collectionName));
			viewSourceDatum = CStringGetTextDatum(definition.viewSource);

			if (strcmp(definition.viewSource, viewName) == 0)
			{
				cycleDetected = true;
				break;
			}

			if (definition.pipeline.value_type != BSON_TYPE_EOD)
			{
				WalkPipelineForViewCycles(databaseDatum, viewName, &definition.pipeline);
			}

			collection = GetMongoCollectionOrViewByNameDatum(databaseDatum,
															 viewSourceDatum, NoLock);
		}
		else
		{
			/* Found a base collection - stop */
			break;
		}
	}

	if (cycleDetected)
	{
		/* Found a path from viewName back to viewName */
		const char *databaseStr = TextDatumGetCString(databaseDatum);

		StringInfo errorStr = makeStringInfo();
		appendStringInfo(errorStr, "%s.%s -> ", databaseStr, viewName);

		ListCell *intermediateCell;
		foreach(intermediateCell, intermediateViews)
		{
			const char *intermediateView = lfirst(intermediateCell);
			appendStringInfo(errorStr, "%s.%s -> ", databaseStr, intermediateView);
		}

		appendStringInfo(errorStr, " %s.%s", databaseStr, viewName);
		ereport(ERROR, (errcode(MongoGraphContainsCycle),
						errmsg("View cycle detected: %s", errorStr->data)));
	}
}
