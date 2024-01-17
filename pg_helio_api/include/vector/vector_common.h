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

/* dynamic calculation of nprobes or efSearch depending on collection size */
#define VECTOR_SEARCH_SMALL_COLLECTION_ROWS 10000
#define VECTOR_SEARCH_1M_COLLECTION_ROWS 1000000

/*
 * helioapi.enableVectorHNSWIndex GUC determines vector indexes
 * and queries are enabled in pg_helio_api or not.
 */
extern bool EnableVectorHNSWIndex;

/*
 * GUC to enable vector pre-filtering feature for vector search.
 * This is disabled by default.
 */
extern bool EnableVectorPreFilter;


#endif
