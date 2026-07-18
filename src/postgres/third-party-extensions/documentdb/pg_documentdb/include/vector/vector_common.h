/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/vector/vector_common.h
 *
 * Common definitions for vector indexes.
 *
 *-------------------------------------------------------------------------
 */
#ifndef VECTOR_COMMON__H
#define VECTOR_COMMON__H

/* pgvector VECTOR_MAX_DIM: 16000 */
/* dimensions for type vector cannot exceed 16000 */
#define VECTOR_MAX_DIMENSIONS 16000
#define VECTOR_MAX_DIMENSIONS_NON_COMPRESSED 2000
#define VECTOR_MAX_DIMENSIONS_HALF_COMPRESSED 4000
#define VECTOR_MAX_DIMENSIONS_PQ_COMPRESSED VECTOR_MAX_DIMENSIONS

/* ivfflat parameters */
#define IVFFLAT_DEFAULT_NPROBES 1
#define IVFFLAT_MIN_NPROBES 1
#define IVFFLAT_MAX_NPROBES 32768

#define IVFFLAT_DEFAULT_LISTS 100
#define IVFFLAT_MIN_LISTS 1
#define IVFFLAT_MAX_LISTS 32768

#define VECTOR_PARAMETER_NAME_IVF_NPROBES "nProbes"
#define VECTOR_PARAMETER_NAME_IVF_NPROBES_STR_LEN 7
#define VECTOR_PARAMETER_NAME_IVF_NLISTS "numLists"

/* hnsw parameters */
#define HNSW_DEFAULT_M 16
#define HNSW_MIN_M 2
#define HNSW_MAX_M 100

#define HNSW_DEFAULT_EF_CONSTRUCTION 64
#define HNSW_MIN_EF_CONSTRUCTION 4
#define HNSW_MAX_EF_CONSTRUCTION 1000

#define HNSW_DEFAULT_EF_SEARCH 40
#define HNSW_MIN_EF_SEARCH 1
#define HNSW_MAX_EF_SEARCH 1000

#define VECTOR_PARAMETER_NAME_HNSW_M "m"
#define VECTOR_PARAMETER_NAME_HNSW_EF_CONSTRUCTION "efConstruction"
#define VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH "efSearch"
#define VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH_STR_LEN 8

/* Search parameter name for iterative scan mode */
#define VECTOR_PARAMETER_NAME_ITERATIVE_SCAN "iterativeScan"
#define VECTOR_PARAMETER_NAME_ITERATIVE_SCAN_STR_LEN 13

/* dynamic calculation of nprobes or efSearch depending on collection size */
#define VECTOR_SEARCH_SMALL_COLLECTION_ROWS 10000

/* metadata field names */
#define VECTOR_METADATA_FIELD_NAME "__cosmos_meta__"
#define VECTOR_METADATA_FIELD_NAME_STR_LEN 15
#define VECTOR_METADATA_SCORE_FIELD_NAME "score"
#define VECTOR_METADATA_SCORE_FIELD_NAME_STR_LEN 5

/*
 * ApiGucPrefix.enableVectorHNSWIndex GUC determines vector indexes
 * and queries are enabled in documentdb_api or not.
 */
extern bool EnableVectorHNSWIndex;

/*
 * GUC to enable vector pre-filtering feature for vector search.
 * This is disabled by default.
 */
extern bool EnableVectorPreFilter;
extern bool EnableVectorPreFilterV2;

/*
 * GUC to set the iterative scan mode for pre-filtering
 */
extern int VectorPreFilterIterativeScanMode;

/*
 * GUC to enable vector compression feature for vector search.
 */
extern bool EnableVectorCompressionHalf;
extern bool EnableVectorCompressionPQ;

/*
 * GUC to enable vector search default search parameter calculation.
 */
extern bool EnableVectorCalculateDefaultSearchParameter;

#endif
