/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/vector/vector_index_kind_impl.c
 *
 * Utility functions related to kind of vector indexes.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <string.h>
#include <math.h>
#include <nodes/makefuncs.h>
#include <utils/builtins.h>
#include <utils/rel.h>
#include <catalog/pg_type.h>
#include <catalog/pg_operator.h>
#include <utils/guc.h>
#include <utils/guc_utils.h>
#include <miscadmin.h>
#include <utils/memutils.h>

#include "io/bson_core.h"
#include "metadata/collection.h"
#include "metadata/index.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"
#include "utils/feature_counter.h"
#include "vector/vector_common.h"
#include "vector/vector_spec.h"
#include "vector/vector_utilities.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* IVFFlat index options
 * Copy of VectorOptions for IVFFlat from PGVector
 * CodeSync: Keep in sync with pgvector.
 */
typedef struct PgVectorIvfflatOptions
{
	int32 vl_len_;              /* varlena header (do not touch directly!) */
	int lists;                  /* number of lists */
} PgVectorIvfflatOptions;


typedef struct PgVectorHnswOptions
{
	int32 vl_len_;              /* varlena header (do not touch directly!) */
	int m;                      /* Total number of active connections */
	int efConstruction;         /* size of dynamic candidate list */
} PgVectorHnswOptions;

typedef struct VectorIVFIndexOptions
{
	/* The number of lists for the ivfflat blocks */
	int32_t numLists;
} VectorIVFIndexOptions;

typedef struct VectorHNSWIndexOptions
{
	/* The m for the HNSW blocks */
	int32_t m;

	/* The efConstruction for the HNSW blocks */
	int32_t efConstruction;
} VectorHNSWIndexOptions;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void ParseIVFCreationSpec(bson_iter_t *vectorOptionsIter,
								 CosmosSearchOptions *cosmosSearchOptions);

static void ParseHNSWCreationSpec(bson_iter_t *vectorOptionsIter,
								  CosmosSearchOptions *cosmosSearchOptions);

static char * GenerateIVFIndexParamStr(const CosmosSearchOptions *cosmosSearchOptions);

static char * GenerateHNSWIndexParamStr(const CosmosSearchOptions *cosmosSearchOptions);

static pgbson * ParseIVFIndexSearchSpec(const VectorSearchOptions *vectorSearchOptions);

static pgbson * ParseHNSWIndexSearchSpec(const VectorSearchOptions *vectorSearchOptions);

static void SetIVFSearchParametersToGUC(const pgbson *searchParamBson);

static void SetHNSWSearchParametersToGUC(const pgbson *searchParamBson);

static pgbson * CalculateIVFSearchParamBson(bytea *indexOptions, Cardinality indexRows,
											pgbson *searchParamBson);

static pgbson * CalculateHNSWSearchParamBson(bytea *indexOptions, Cardinality indexRows,
											 pgbson *searchParamBson);

static VectorIndexCompressionType ExtractIVFCompressionType(bytea *indexOptions);

static VectorIndexCompressionType ExtractHNSWCompressionType(bytea *indexOptions);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

static VectorIndexDefinition VectorIndexDefinitionArray[] = {
	{
		.kindName = "vector-ivf",
		.indexAccessMethodName = "ivfflat",
		.parseIndexCreationSpecFunc = &ParseIVFCreationSpec,
		.generateIndexParamStrFunc = &GenerateIVFIndexParamStr,
		.parseIndexSearchSpecFunc = &ParseIVFIndexSearchSpec,
		.getIndexAccessMethodOidFunc = &PgVectorIvfFlatIndexAmId,
		.setSearchParametersToGUCFunc = &SetIVFSearchParametersToGUC,
		.calculateSearchParamBsonFunc = &CalculateIVFSearchParamBson,
		.extractIndexCompressionTypeFunc = &ExtractIVFCompressionType
	},
	{
		.kindName = "vector-hnsw",
		.indexAccessMethodName = "hnsw",
		.parseIndexCreationSpecFunc = &ParseHNSWCreationSpec,
		.generateIndexParamStrFunc = &GenerateHNSWIndexParamStr,
		.parseIndexSearchSpecFunc = &ParseHNSWIndexSearchSpec,
		.getIndexAccessMethodOidFunc = &PgVectorHNSWIndexAmId,
		.setSearchParametersToGUCFunc = &SetHNSWSearchParametersToGUC,
		.calculateSearchParamBsonFunc = &CalculateHNSWSearchParamBson,
		.extractIndexCompressionTypeFunc = &ExtractHNSWCompressionType
	},
	{ 0 },
	{ 0 },
	{ 0 },
};

static int NumberOfVectorIndexDefinitions = 2;

static const int MaxNumberOfVectorIndexDefinitions = sizeof(VectorIndexDefinitionArray) /
													 sizeof(VectorIndexDefinition);


const VectorIndexDefinition *
GetVectorIndexDefinitionByIndexAmOid(Oid indexAmOid)
{
	for (int i = 0; i < NumberOfVectorIndexDefinitions; i++)
	{
		if (VectorIndexDefinitionArray[i].getIndexAccessMethodOidFunc() == indexAmOid)
		{
			return &VectorIndexDefinitionArray[i];
		}
	}

	return NULL;
}


const VectorIndexDefinition *
GetVectorIndexDefinitionByIndexKindName(StringView *indexKindStr)
{
	for (int i = 0; i < NumberOfVectorIndexDefinitions; i++)
	{
		if (StringViewEqualsCString(indexKindStr,
									VectorIndexDefinitionArray[i].kindName))
		{
			return &VectorIndexDefinitionArray[i];
		}
	}

	return NULL;
}


void
RegisterVectorIndexExtension(const VectorIndexDefinition *extensibleDefinition)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR, (errmsg(
							"Vector index extensions can only be added during shared_preload_libraries")));
	}


	if (NumberOfVectorIndexDefinitions == MaxNumberOfVectorIndexDefinitions)
	{
		ereport(ERROR, (errmsg("Max vector extensions registered reached.")));
	}

	if (extensibleDefinition->kindName == NULL)
	{
		ereport(ERROR, (errmsg("No kind name specified for extensible definition")));
	}

	if (extensibleDefinition->parseIndexCreationSpecFunc == NULL)
	{
		ereport(ERROR, (errmsg("No parsing function for search index kind %s",
							   extensibleDefinition->kindName)));
	}

	if (extensibleDefinition->indexAccessMethodName == NULL ||
		extensibleDefinition->generateIndexParamStrFunc == NULL)
	{
		ereport(ERROR, (errmsg(
							"No getIndexAccessMethodNameFunc or generateIndexParamStrFunc defined for index kind %s",
							extensibleDefinition->kindName)));
	}

	if (extensibleDefinition->parseIndexSearchSpecFunc == NULL)
	{
		ereport(ERROR, (errmsg("No parsing function for search index kind %s",
							   extensibleDefinition->kindName)));
	}

	if (extensibleDefinition->setSearchParametersToGUCFunc == NULL)
	{
		ereport(ERROR, (errmsg(
							"setSearchParametersToGUCFunc is not defined for the vector index")));
	}

	if (extensibleDefinition->calculateSearchParamBsonFunc == NULL)
	{
		ereport(ERROR, (errmsg(
							"calculateSearchParamBsonFunc is not defined for the vector index type: %s",
							extensibleDefinition->kindName)));
	}

	if (extensibleDefinition->extractIndexCompressionTypeFunc == NULL)
	{
		ereport(ERROR, (errmsg(
							"extractIndexCompressionTypeFunc is not defined for the vector index type: %s",
							extensibleDefinition->kindName)));
	}

	VectorIndexDefinitionArray[NumberOfVectorIndexDefinitions] = *extensibleDefinition;
	NumberOfVectorIndexDefinitions++;
}


/*
 * Functions handle the creation options for the vector index on coordinator.
 *      1. Parser functions parse the vector index creation options
 *      2. Validator functions validate the vector index creation options
 *      3. Generator functions generate the vector index creation cmd
 */

/*
 * Parse the options for the IVF index creation on coordinator.
 */
static void
ParseIVFCreationSpec(bson_iter_t *vectorOptionsIter,
					 CosmosSearchOptions *cosmosSearchOptions)
{
	/* IVF does not support compression type: pq */
	if (cosmosSearchOptions->commonOptions.compressionType ==
		VectorIndexCompressionType_PQ)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
						errmsg("Compression type 'pq' is not supported for ivf index")));
	}

	ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR_TYPE_IVFFLAT);
	Assert(cosmosSearchOptions->indexKindStr == VectorIndexDefinitionArray[0].kindName);

	VectorIVFIndexOptions *vectorIndexOptions = palloc0(sizeof(VectorIVFIndexOptions));
	cosmosSearchOptions->vectorOptions =
		(VectorKindSpecifiedOptions *) vectorIndexOptions;

	while (bson_iter_next(vectorOptionsIter))
	{
		const char *optionsIterKey = bson_iter_key(vectorOptionsIter);

		const bson_value_t *keyValue = bson_iter_value(vectorOptionsIter);

		if (strcmp(optionsIterKey, VECTOR_PARAMETER_NAME_IVF_NLISTS) == 0)
		{
			if (!BsonValueIsNumber(keyValue))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg("%s must be a number not %s",
									   VECTOR_PARAMETER_NAME_IVF_NLISTS,
									   BsonTypeName(bson_iter_type(vectorOptionsIter)))));
			}

			vectorIndexOptions->numLists = BsonValueAsInt32(keyValue);

			if (vectorIndexOptions->numLists < IVFFLAT_MIN_LISTS)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg(
									"%s must be greater than or equal to %d, but was %d",
									VECTOR_PARAMETER_NAME_IVF_NLISTS,
									vectorIndexOptions->numLists,
									IVFFLAT_MIN_LISTS)));
			}

			if (vectorIndexOptions->numLists > IVFFLAT_MAX_LISTS)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg(
									"Current value of %s is %d, which exceeds the max value of %d",
									VECTOR_PARAMETER_NAME_IVF_NLISTS,
									vectorIndexOptions->numLists,
									IVFFLAT_MAX_LISTS)));
			}
		}
	}

	/* Set default numLists for ivfflat */
	if (vectorIndexOptions->numLists == 0)
	{
		vectorIndexOptions->numLists = IVFFLAT_DEFAULT_LISTS;
	}
}


/*
 * Parse the options for the HNSW index creation on coordinator.
 */
static void
ParseHNSWCreationSpec(bson_iter_t *vectorOptionsIter,
					  CosmosSearchOptions *cosmosSearchOptions)
{
	if (!EnableVectorHNSWIndex)
	{
		/* Safe guard against the ApiGucPrefix.enableVectorHNSWIndex GUC */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"hnsw index is not supported for this cluster tier")));
	}

	/* HNSW does not support compression type: pq */
	if (cosmosSearchOptions->commonOptions.compressionType ==
		VectorIndexCompressionType_PQ)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
						errmsg("Compression type 'pq' is not supported for hnsw index")));
	}

	ReportFeatureUsage(FEATURE_CREATE_INDEX_VECTOR_TYPE_HNSW);
	Assert(cosmosSearchOptions->indexKindStr == VectorIndexDefinitionArray[1].kindName);

	VectorHNSWIndexOptions *vectorIndexOptions = palloc0(sizeof(VectorHNSWIndexOptions));
	cosmosSearchOptions->vectorOptions =
		(VectorKindSpecifiedOptions *) vectorIndexOptions;

	while (bson_iter_next(vectorOptionsIter))
	{
		const char *optionsIterKey = bson_iter_key(vectorOptionsIter);

		const bson_value_t *keyValue = bson_iter_value(vectorOptionsIter);

		if (strcmp(optionsIterKey, VECTOR_PARAMETER_NAME_HNSW_M) == 0)
		{
			if (!BsonValueIsNumber(keyValue))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg("%s must be a number not %s",
									   VECTOR_PARAMETER_NAME_HNSW_M,
									   BsonTypeName(bson_iter_type(vectorOptionsIter)))));
			}

			vectorIndexOptions->m = BsonValueAsInt32(keyValue);

			if (vectorIndexOptions->m < HNSW_MIN_M)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg(
									"%s must be greater than or equal to %d, but was %d",
									VECTOR_PARAMETER_NAME_HNSW_M,
									HNSW_MIN_M,
									vectorIndexOptions->m)));
			}

			if (vectorIndexOptions->m > HNSW_MAX_M)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg(
									"%s should not exceed %d, but received %d",
									VECTOR_PARAMETER_NAME_HNSW_M,
									HNSW_MAX_M,
									vectorIndexOptions->m)));
			}
		}
		else if (strcmp(optionsIterKey,
						VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION) == 0)
		{
			if (!BsonValueIsNumber(keyValue))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg("%s must be a number not %s",
									   VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION,
									   BsonTypeName(bson_iter_type(vectorOptionsIter)))));
			}

			vectorIndexOptions->efConstruction = BsonValueAsInt32(
				keyValue);

			if (vectorIndexOptions->efConstruction < HNSW_MIN_EF_CONSTRUCTION)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg(
									"%s must be greater than or equal to %d, but was %d",
									VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION,
									HNSW_MIN_EF_CONSTRUCTION,
									vectorIndexOptions->efConstruction)));
			}

			if (vectorIndexOptions->efConstruction > HNSW_MAX_EF_CONSTRUCTION)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
								errmsg(
									"%s should not exceed %d, but received %d",
									VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION,
									HNSW_MAX_EF_CONSTRUCTION,
									vectorIndexOptions->efConstruction)));
			}
		}
	}

	/* Default efConstruction value set for hnsw */
	if (vectorIndexOptions->efConstruction == 0)
	{
		vectorIndexOptions->efConstruction = HNSW_DEFAULT_EF_CONSTRUCTION;
	}

	/* Set default m for hnsw */
	if (vectorIndexOptions->m == 0)
	{
		vectorIndexOptions->m = HNSW_DEFAULT_M;
	}

	/* Check efConstruction is greater than or equal to m * 2 */
	if ((vectorIndexOptions->efConstruction < vectorIndexOptions->m * 2))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_CANNOTCREATEINDEX),
						errmsg(
							"%s must be greater than or equal to 2 * m for vector-hnsw indexes",
							VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION)));
	}
}


static char *
GenerateIVFIndexParamStr(const CosmosSearchOptions *cosmosSearchOptions)
{
	Assert(cosmosSearchOptions->indexKindStr == VectorIndexDefinitionArray[0].kindName);

	VectorIVFIndexOptions *vectorOptions =
		(VectorIVFIndexOptions *) cosmosSearchOptions->vectorOptions;
	StringInfo paramStr = makeStringInfo();

	appendStringInfo(paramStr, "lists = %d", vectorOptions->numLists);

	return paramStr->data;
}


static char *
GenerateHNSWIndexParamStr(const CosmosSearchOptions *cosmosSearchOptions)
{
	Assert(cosmosSearchOptions->indexKindStr == VectorIndexDefinitionArray[1].kindName);

	VectorHNSWIndexOptions *vectorOptions =
		(VectorHNSWIndexOptions *) cosmosSearchOptions->vectorOptions;
	StringInfo paramStr = makeStringInfo();

	appendStringInfo(paramStr, "m = %d, ef_construction = %d",
					 vectorOptions->m, vectorOptions->efConstruction);

	return paramStr->data;
}


/*
 * Functions handle the search options for the vector index on coordinator.
 *      1. Parser functions parse the vector index search options
 *      2. Validator functions validate the vector index search options
 */

/*
 * Parse the options for the IVF index search on coordinator.
 */
static pgbson *
ParseIVFIndexSearchSpec(const VectorSearchOptions *vectorSearchOptions)
{
	ReportFeatureUsage(FEATURE_STAGE_SEARCH_VECTOR_IVFFLAT);

	pgbson *vectorSearchSpecPgbson = vectorSearchOptions->searchSpecPgbson;
	bson_iter_t specIter;
	PgbsonInitIterator(vectorSearchSpecPgbson, &specIter);

	pgbson *searchSpec = NULL;
	while (bson_iter_next(&specIter))
	{
		const char *key = bson_iter_key(&specIter);
		const bson_value_t *value = bson_iter_value(&specIter);

		if (strcmp(key, VECTOR_PARAMETER_NAME_IVF_NPROBES) == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&specIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$nProbes is required to be an integer value.")));
			}

			int32_t nProbes = BsonValueAsInt32(value);

			if (nProbes < IVFFLAT_MIN_NPROBES)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"The value of $nProbes should be at least %d.",
									IVFFLAT_MIN_NPROBES)));
			}

			if (nProbes > IVFFLAT_MAX_NPROBES)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"The value of $nProbes must not exceed %d.",
									IVFFLAT_MAX_NPROBES)));
			}

			if (searchSpec != NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("Only one search option can be specified. "
									   "You have specified options nProbes already,"
									   " and the second option nProbes is not allowed.")));
			}

			pgbson_writer writer;
			PgbsonWriterInit(&writer);
			PgbsonWriterAppendValue(&writer, key, bson_iter_key_len(
										&specIter), value);
			searchSpec = PgbsonWriterGetPgbson(&writer);
		}
	}

	return searchSpec;
}


/*
 * Parse the options for the HNSW index search on coordinator.
 */
static pgbson *
ParseHNSWIndexSearchSpec(const VectorSearchOptions *vectorSearchOptions)
{
	if (!EnableVectorHNSWIndex)
	{
		/* Safe guard against the ApiGucPrefix.enableVectorHNSWIndex GUC */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"HNSW index type is currently unsupported"),
						errdetail(
							"hnsw index configuration is not enabled. Set ApiGucPrefix.enableVectorHNSWIndex to true to enable hnsw index.")));
	}

	ReportFeatureUsage(FEATURE_STAGE_SEARCH_VECTOR_HNSW);

	pgbson *vectorSearchSpecPgbson = vectorSearchOptions->searchSpecPgbson;

	bson_iter_t specIter;
	PgbsonInitIterator(vectorSearchSpecPgbson, &specIter);

	pgbson *searchSpec = NULL;
	while (bson_iter_next(&specIter))
	{
		const char *key = bson_iter_key(&specIter);
		const bson_value_t *value = bson_iter_value(&specIter);

		if (strcmp(key, VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH) == 0)
		{
			if (!BSON_ITER_HOLDS_NUMBER(&specIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$efSearch must be an integer value.")));
			}

			int32_t efSearch = BsonValueAsInt32(value);

			if (efSearch < HNSW_MIN_EF_SEARCH)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"The parameter $efSearch should have a value that is greater than or equal to %d.",
									HNSW_MIN_EF_SEARCH)));
			}

			if (efSearch > HNSW_MAX_EF_SEARCH)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"$efSearch must be less than or equal to %d.",
									HNSW_MAX_EF_SEARCH)));
			}

			if (searchSpec != NULL)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("Only one search option can be specified. "
									   "You have specified options efSearch already, "
									   "and the second option efSearch is not allowed.")));
			}

			pgbson_writer writer;
			PgbsonWriterInit(&writer);
			PgbsonWriterAppendValue(&writer, key, bson_iter_key_len(
										&specIter), value);
			searchSpec = PgbsonWriterGetPgbson(&writer);
		}
	}

	return searchSpec;
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */
static pgbson *
CalculateIVFSearchParamBson(bytea *indexOptions, Cardinality indexRows,
							pgbson *searchParamBson)
{
	/* If the searchParamBson already has the nProbes */
	bson_iter_t documentIterator;
	if (searchParamBson != NULL &&
		PgbsonInitIteratorAtPath(searchParamBson, VECTOR_PARAMETER_NAME_IVF_NPROBES,
								 &documentIterator))
	{
		return searchParamBson;
	}

	ReportFeatureUsage(FEATURE_STAGE_SEARCH_VECTOR_DEFAULT_NPROBES);

	/* Calculate the default nProbes */
	int numLists = -1;
	if (indexOptions == NULL)
	{
		numLists = IVFFLAT_DEFAULT_LISTS;
	}
	else
	{
		PgVectorIvfflatOptions *options =
			(PgVectorIvfflatOptions *) indexOptions;
		numLists = options->lists;
	}

	int defaultNumProbes = -1;
	if (numLists <= 0)
	{
		defaultNumProbes = IVFFLAT_DEFAULT_NPROBES;
	}
	else
	{
		if (EnableVectorCalculateDefaultSearchParameter)
		{
			/* nProbes < 10000 rows: numLists */
			if (indexRows < VECTOR_SEARCH_SMALL_COLLECTION_ROWS)
			{
				defaultNumProbes = numLists;
			}
			else
			{
				/* >= 10000 rows: we calculate nProbes based on estimated rowsPerCluster
				 * To make sure we don't scan too many rows */
				double rowsPerCluster = indexRows / numLists;
				defaultNumProbes = ceil(VECTOR_SEARCH_SMALL_COLLECTION_ROWS /
										rowsPerCluster);
				if (defaultNumProbes > numLists)
				{
					defaultNumProbes = numLists;
				}

				if (defaultNumProbes < 1)
				{
					defaultNumProbes = 1;
				}
			}
		}
		else
		{
			defaultNumProbes = IVFFLAT_DEFAULT_NPROBES;
		}
	}

	pgbson_writer optionsWriter;
	PgbsonWriterInit(&optionsWriter);
	if (defaultNumProbes != -1)
	{
		PgbsonWriterAppendInt32(&optionsWriter, VECTOR_PARAMETER_NAME_IVF_NPROBES,
								VECTOR_PARAMETER_NAME_IVF_NPROBES_STR_LEN,
								defaultNumProbes);
	}

	if (searchParamBson != NULL)
	{
		PgbsonWriterConcat(&optionsWriter, searchParamBson);
	}

	return PgbsonWriterGetPgbson(&optionsWriter);
}


static pgbson *
CalculateHNSWSearchParamBson(bytea *indexOptions, Cardinality indexRows,
							 pgbson *searchParamBson)
{
	/* If the searchParamBson already has the efSearch */
	bson_iter_t documentIterator;
	if (searchParamBson != NULL &&
		PgbsonInitIteratorAtPath(searchParamBson, VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH,
								 &documentIterator))
	{
		return searchParamBson;
	}

	ReportFeatureUsage(FEATURE_STAGE_SEARCH_VECTOR_DEFAULT_EFSEARCH);

	/* Calculate the default efSearch */
	int efConstruction = -1;

	if (indexOptions == NULL)
	{
		efConstruction = HNSW_DEFAULT_EF_CONSTRUCTION;
	}
	else
	{
		PgVectorHnswOptions *options =
			(PgVectorHnswOptions *) indexOptions;
		efConstruction = options->efConstruction;
	}

	int defaultEfSearch = -1;
	if (efConstruction < 0)
	{
		defaultEfSearch = HNSW_DEFAULT_EF_SEARCH;
	}
	else
	{
		if (EnableVectorCalculateDefaultSearchParameter)
		{
			if (indexRows < VECTOR_SEARCH_SMALL_COLLECTION_ROWS)
			{
				defaultEfSearch = efConstruction;
			}
			else
			{
				defaultEfSearch = HNSW_DEFAULT_EF_SEARCH;
			}
		}
		else
		{
			defaultEfSearch = HNSW_DEFAULT_EF_SEARCH;
		}
	}

	pgbson_writer optionsWriter;
	PgbsonWriterInit(&optionsWriter);
	if (defaultEfSearch != -1)
	{
		PgbsonWriterAppendInt32(&optionsWriter, VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH,
								VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH_STR_LEN,
								defaultEfSearch);
	}

	if (searchParamBson != NULL)
	{
		PgbsonWriterConcat(&optionsWriter, searchParamBson);
	}

	return PgbsonWriterGetPgbson(&optionsWriter);
}


static void
SetIVFSearchParametersToGUC(const pgbson *searchParamBson)
{
	bson_iter_t documentIterator;
	PgbsonInitIterator(searchParamBson, &documentIterator);
	while (bson_iter_next(&documentIterator))
	{
		const char *key = bson_iter_key(&documentIterator);
		const bson_value_t *value = bson_iter_value(&documentIterator);
		if (strcmp(key, VECTOR_PARAMETER_NAME_IVF_NPROBES) == 0)
		{
			int32_t nProbes = BsonValueAsInt32(bson_iter_value(
												   &documentIterator));

			/*
			 * set nProbes to local GUC ivfflat.probes
			 */
			char nProbesStr[20];
			snprintf(nProbesStr, sizeof(nProbesStr), "%d",
					 nProbes);
			SetGUCLocally("ivfflat.probes", nProbesStr);
		}
		else if (strcmp(key, VECTOR_PARAMETER_NAME_ITERATIVE_SCAN) == 0)
		{
			if (value->value_type == BSON_TYPE_UTF8)
			{
				/*
				 * set local ivfflat.iterative_scan=relaxed_order
				 * TODO: set pgvector GUCs for ivf iterative scan
				 *      ivfflat.max_probes: Specify the max number of probes(32768 by default)
				 */
				const char *iterativeScanMode = value->value.v_utf8.str;

				/* The default of GUC VectorPreFilterIterativeScanMode is relaxed_order,
				 * in case we set it to strict_order, it is not supported for the ivf index, use relaxed_order instead
				 * this may cause the results to be slightly out of order */
				if (strcmp(iterativeScanMode, "strict_order") == 0)
				{
					ereport(WARNING, errmsg(
								"Iiterative_scan '%s' is not supported for ivf index, use relaxed_order instead, this may cause the results to be slightly out of order.",
								iterativeScanMode));
					iterativeScanMode = "relaxed_order";
				}

				SetGUCLocally("ivfflat.iterative_scan", iterativeScanMode);
			}
		}
	}
}


static void
SetHNSWSearchParametersToGUC(const pgbson *searchParamBson)
{
	bson_iter_t documentIterator;
	PgbsonInitIterator(searchParamBson, &documentIterator);
	while (bson_iter_next(&documentIterator))
	{
		const char *key = bson_iter_key(&documentIterator);
		const bson_value_t *value = bson_iter_value(&documentIterator);
		if (strcmp(key, VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH) == 0)
		{
			int32_t efSearch = BsonValueAsInt32(value);

			/*
			 * set efSearch to local GUC hnsw.ef_search
			 */
			char efSearchStr[20];
			snprintf(efSearchStr, sizeof(efSearchStr), "%d",
					 efSearch);
			SetGUCLocally("hnsw.ef_search", efSearchStr);
		}
		else if (strcmp(key, VECTOR_PARAMETER_NAME_ITERATIVE_SCAN) == 0)
		{
			if (value->value_type == BSON_TYPE_UTF8)
			{
				/*
				 * set local hnsw.iterative_scan=relaxed_order
				 * TODO: set pgvector GUCs for hnsw iterative scan
				 *  hnsw.max_scan_tuples: Specify the max number of tuples to visit (20,000 by default)
				 *  hnsw.scan_mem_multiplier: Specify the max amount of memory to use, as a multiple of work_mem (1 by default)
				 */
				SetGUCLocally("hnsw.iterative_scan", value->value.v_utf8.str);
			}
		}
	}
}


static VectorIndexCompressionType
ExtractIVFCompressionType(bytea *indexOptions)
{
	/* Retrieve the compression type from the index options */
	/* Currently, there is no index options for compression type */
	return VectorIndexCompressionType_None;
}


static VectorIndexCompressionType
ExtractHNSWCompressionType(bytea *indexOptions)
{
	/* Retrieve the compression type from the index options */
	/* Currently, there is no index options for compression type */
	return VectorIndexCompressionType_None;
}
