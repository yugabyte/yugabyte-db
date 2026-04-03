/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/vector/vector_spec.h
 *
 * Common definitions for vector indexes.
 *
 *-------------------------------------------------------------------------
 */
#ifndef VECTOR_SPEC__H
#define VECTOR_SPEC__H

#include <nodes/primnodes.h>
#include <utils/relcache.h>

#include "io/bson_core.h"

typedef struct VectorIndexDefinition VectorIndexDefinition;

/*
 * The distance metric for a vector based index.
 */
typedef enum VectorIndexDistanceMetric
{
	VectorIndexDistanceMetric_Unknown = 0,

	/* Use basic linear vector distance */
	VectorIndexDistanceMetric_L2Distance = 1,

	/* Use vector inner product distance */
	VectorIndexDistanceMetric_IPDistance = 2,

	/* Use inner product cosine distance. */
	VectorIndexDistanceMetric_CosineDistance = 3,
} VectorIndexDistanceMetric;

/*
 * The compression type for a vector index.
 */
typedef enum VectorIndexCompressionType
{
	/* Use no compression */
	VectorIndexCompressionType_None = 0,

	/* Use half precision compression */
	VectorIndexCompressionType_Half = 1,

	/* Use product quantization */
	VectorIndexCompressionType_PQ = 2,

	/* Use binary quantization */
	VectorIndexCompressionType_BQ = 3,
} VectorIndexCompressionType;

/*
 * Options associated with vector based Cosmos Search indexes
 */
typedef struct VectorIndexCommonOptions
{
	/* The type of distance for the vector distance */
	VectorIndexDistanceMetric distanceMetric;

	/* The type of compression for the vector index */
	VectorIndexCompressionType compressionType;

	/* The number of dimensions of the vector */
	int32_t numDimensions;
} VectorIndexCommonOptions;

/*
 * Options for a vector index that specifies the kind of index.
 */
typedef struct VectorKindSpecifiedOptions
{ } VectorKindSpecifiedOptions;

/*
 * Index options specific to cosmosSearchOptions
 * for vector and text search support.
 */
typedef struct
{
	/* The raw pgbson for the cosmosSearchOptions. */
	pgbson *searchOptionsDoc;

	/* The index kind for the cosmosSearch index. */
	const char *indexKindStr;

	/* Options for a vector search */
	VectorIndexCommonOptions commonOptions;
	VectorKindSpecifiedOptions *vectorOptions;
} CosmosSearchOptions;

typedef struct VectorSearchOptions
{
	/* it's the query spec pgbson that we pass to the bson_extract_vector() method to get the float[] vector. */
	pgbson *searchSpecPgbson;

	/* the bson value for the query vector */
	bson_value_t queryVectorValue;

	/* the length of the query vector */
	int32_t queryVectorLength;

	/* search path*/
	char *searchPath;

	/* query result count */
	int32_t resultCount;

	/* search param pgbson e.g. {"efSearch": 10} or {"nProbes": 10} */
	pgbson *searchParamPgbson;

	/* filter bson */
	bson_value_t filterBson;

	/* score bson */
	bson_value_t scoreBson;

	/* The vector access method oid */
	Oid vectorAccessMethodOid;

	/* whether to use exact search or ann search */
	bool exactSearch;

	/* The vector index definition */
	const VectorIndexDefinition *vectorIndexDef;

	/* Over sample rate */
	double oversampling;

	/* The compression type of the vector index */
	VectorIndexCompressionType compressionType;

	/* The type of distance for the vector distance */
	VectorIndexDistanceMetric distanceMetric;
} VectorSearchOptions;

/*
 * Function pointers for parsing and validating
 * vector index creation specs on coordinator.
 */
typedef void (*ParseIndexCreationSpecFunc)(bson_iter_t *indexDefDocIter,
										   CosmosSearchOptions *cosmosSearchOptions);

typedef char *
(*GenerateIndexParamStringFunc)(const CosmosSearchOptions *cosmosSearchOptions);

/*
 * Function pointers for parsing and validating
 * vector index search specs on coordinator.
 */
typedef pgbson *(*ParseIndexSearchSpecFunc)(const
											VectorSearchOptions *vectorSearchOptions);

/*
 * Function pointers for planner and custom scan
 */
typedef Oid (*GetIndexAccessMethodOidFunc)(void);

/*
 * Function pointers for setting search parameters to GUC on worker.
 */
typedef void (*SetSearchParametersToGUCFunc)(const pgbson *searchParamBson);

/*
 * Dynamic calculation of search parameters
 * based on the number of rows and index options.
 */
typedef pgbson *(*CalculateSearchParamBsonFunc)(bytea *indexOptions,
												Cardinality indexRows,
												pgbson *searchParamBson);

/*
 * Retrieve the type of index compression specified within the provided index options.
 */
typedef VectorIndexCompressionType (*ExtractIndexCompressionTypeFunc)(
	bytea *indexOptions);

/*
 * Definition of an extensible vector index.
 * It contains the function pointers for
 * 1. Vector index creation
 *  1.1 Parsing and validating index creation specs.
 *  1.2 Generating index parameter for the index creation.
 * 2. Vector search
 *  2.1 Parsing and validating index search specs.
 *  2.2 Getting the index access method oid.
 *  2.3 Getting the similarity operator oid by family oid.
 * 3. Planner and custom scan
 *  3.1 Setting search parameters to GUC on worker.
 *  3.2 Getting the default search parameter bson.
 *  3.3 Calculating search parameter based on the number of rows and index options.
 */
typedef struct VectorIndexDefinition
{
	const char *kindName;

	const char *indexAccessMethodName;

	ParseIndexCreationSpecFunc parseIndexCreationSpecFunc;

	GenerateIndexParamStringFunc generateIndexParamStrFunc;

	ParseIndexSearchSpecFunc parseIndexSearchSpecFunc;

	GetIndexAccessMethodOidFunc getIndexAccessMethodOidFunc;

	SetSearchParametersToGUCFunc setSearchParametersToGUCFunc;

	CalculateSearchParamBsonFunc calculateSearchParamBsonFunc;

	ExtractIndexCompressionTypeFunc extractIndexCompressionTypeFunc;
} VectorIndexDefinition;

const VectorIndexDefinition * GetVectorIndexDefinitionByIndexAmOid(Oid indexAmOid);

const VectorIndexDefinition * GetVectorIndexDefinitionByIndexKindName(
	StringView *indexKindStr);

void RegisterVectorIndexExtension(const VectorIndexDefinition *extensibleDefinition);


#endif
