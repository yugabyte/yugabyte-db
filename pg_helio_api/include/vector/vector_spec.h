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

#include "io/helio_bson_core.h"

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
 * Options associated with vector based Cosmos Search indexes
 */
typedef struct VectorIndexCommonOptions
{
	/* The type of distance for the vector distance */
	VectorIndexDistanceMetric distanceMetric;

	/* The number of dimensions of the vector */
	int32_t numDimensions;
} VectorIndexCommonOptions;

/*
 * Options for a vector index that specifies the kind of index.
 */
typedef struct VectorKindSpecifiedOptions
{ } VectorKindSpecifiedOptions;

/*
 * Options specific to Cosmos specific indexing
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

/*
 * Function pointers for parsing and validating
 * vector index creation specs on coordinator.
 */
typedef void (*ParseIndexCreationSpecFunc)(bson_iter_t *indexDefDocIter,
										   CosmosSearchOptions *cosmosSearchOptions);

typedef char *
(*GenerateIndexParamStringFunc)(const CosmosSearchOptions *searchOptions);

/*
 * Function pointers for parsing and validating
 * vector index search specs on coordinator.
 */
typedef pgbson *(*ParseIndexSearchSpecFunc)(const pgbson *vectorSearchSpecPgbson);

/*
 * Function pointers for planner and custom scan
 */
typedef Oid (*GetIndexAccessMethodOidFunc)(void);

typedef Oid (*GetSimilarityOpOidByFamilyOidFunc)(Oid operatorFamilyOid);

/*
 * Function pointers for setting search parameters to GUC on worker.
 */
typedef void (*SetSearchParametersToGUCFunc)(const pgbson *searchParamBson);

/*
 * Get the default SearchParamBson for the vector index.
 */
typedef pgbson *(*GetDefaultSearchParamBsonFunc)(void);

/*
 * Dynamic calculation of search parameters
 * based on the number of rows and index options.
 */
typedef pgbson *(*CalculateSearchParamBsonFunc)(bytea *indexOptions, Cardinality
												indexRows);

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
typedef struct
{
	const char *kindName;

	const char *indexAccessMethodName;

	ParseIndexCreationSpecFunc parseIndexCreationSpecFunc;

	GenerateIndexParamStringFunc generateIndexParamStrFunc;

	ParseIndexSearchSpecFunc parseIndexSearchSpecFunc;

	GetIndexAccessMethodOidFunc getIndexAccessMethodOidFunc;

	GetSimilarityOpOidByFamilyOidFunc getSimilarityOpOidByFamilyOidFunc;

	SetSearchParametersToGUCFunc setSearchParametersToGUCFunc;

	GetDefaultSearchParamBsonFunc getDefaultSearchParamBsonFunc;

	CalculateSearchParamBsonFunc calculateSearchParamBsonFunc;
} VectorIndexDefinition;

const VectorIndexDefinition * GetVectorIndexDefinitionByIndexAmOid(Oid indexAmOid);

const VectorIndexDefinition * GetVectorIndexDefinitionByIndexKindName(
	StringView *indexKindStr);

void RegisterVectorIndexExtension(const VectorIndexDefinition *extensibleDefinition);


#endif
