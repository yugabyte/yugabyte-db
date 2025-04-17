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

#include <io/bson_core.h>
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
	/* This contains the bson value of the search parameter,
	 * like { "nProbes": 4 } */
	Datum SearchParamBson;

	/* The access method oid of the vector index
	 * ivfflat/hnsw */
	Oid VectorAccessMethodOid;
} SearchQueryEvalData;


double EvaluateMetaSearchScore(pgbson *document);

char * GenerateVectorIndexExprStr(const char *keyPath,
								  const CosmosSearchOptions *searchOptions);

Expr * GenerateVectorSortExpr(const char *queryVectorPath,
							  FuncExpr *vectorCastFunc, Relation indexRelation,
							  Node *documentExpr, Node *vectorQuerySpecNode,
							  bool exactSearch);

bool IsMatchingVectorIndex(Relation indexRelation, const char *queryVectorPath,
						   FuncExpr **vectorExtractorFunc);

#endif
