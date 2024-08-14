/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/vector/vector_utilities.h
 *
 * Utility functions for computing vector scores
 *
 *-------------------------------------------------------------------------
 */
#ifndef VECTOR_UTILITIES__H
#define VECTOR_UTILITIES__H

#include <io/helio_bson_core.h>
#include <fmgr.h>

#include "vector/vector_spec.h"


/*
 * Type that holds data needed for
 * computing scores vector returned from
 * a vector search query. This is used to
 * move required data across nodes via
 * CustomScan.
 */
typedef struct SearchQueryEvalData
{
	/* Oid of the similarity search
	 * operation COS/IP/L2 */
	Oid SimilaritySearchOpOid;

	/* Query Vector Datum which will
	 * be used to calculate score against
	 * the returned vectors */
	Datum QueryVector;

	/* The path containing the vectors in
	 * the documents */
	Datum VectorPathName;

	/* This contains the bson value of the search parameter,
	 * like { "nProbes": 4 } */
	Datum SearchParamBson;

	/* The access method oid of the vector index
	 * ivfflat/hnsw */
	Oid VectorAccessMethodOid;
} SearchQueryEvalData;


/*
 * Type that holds data needed for
 * computing scores vector returned from
 * a vector search query. This is used to
 * move required data across nodes via
 * CustomScan.
 */
typedef struct SearchQueryEvalDataWorker
{
	/* Oid of the similarity search
	 * operation COS/IP/L2 */
	Oid SimilaritySearchOpOid;

	/* The path containing the vectors in
	 * the documents */
	char *VectorPathName;

	/* This is the cached functioncall info
	 * data that can be used on the incoming
	 * documents for computing scores */
	FunctionCallInfoBaseData *SimilarityFuncInfo;
} SearchQueryEvalDataWorker;

FunctionCallInfoBaseData * CreateFCInfoForScoreCalculation(const SearchQueryEvalData
														   *queryEvalData);
double EvaluateMetaSearchScore(pgbson *document);

char * GenerateVectorIndexExprStr(const char *keyPath,
								  const CosmosSearchOptions *searchOptions);

Expr * GenerateVectorSortExpr(const char *queryVectorPath,
							  FuncExpr *vectorCastFunc, Relation indexRelation,
							  Node *documentExpr, Node *vectorQuerySpecNode);

bool IsMatchingVectorIndex(Relation indexRelation, const char *queryVectorPath,
						   FuncExpr **vectorExtractorFunc);

#endif
