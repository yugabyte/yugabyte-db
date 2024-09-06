/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/vector/vector_utilities.c
 *
 * Utility functions related to pgvector operations.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <catalog/pg_operator.h>
#include <catalog/pg_type.h>
#include <nodes/makefuncs.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/rel.h>
#include <utils/syscache.h>
#include <commands/defrem.h>

#include "io/helio_bson_core.h"
#include "utils/mongo_errors.h"
#include "metadata/metadata_cache.h"
#include "vector/bson_extract_vector.h"
#include "vector/vector_common.h"
#include "vector/vector_planner.h"
#include "vector/vector_utilities.h"
#include "vector/vector_spec.h"

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

extern SearchQueryEvalDataWorker *VectorEvaluationData;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static Oid GetSimilarityOperatorOidByFamilyOid(Oid operatorFamilyOid, Oid
											   accessMethodOid);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/*
 * This function creates a FunctionCallInfoBaseData for calling to the
 * vector distance calculation function in pgvector. FunctionCallInfoBaseData
 * stores the query vector and the information about the distance function.
 * FunctionCallInfoBaseData is then cached and later used for each input vector
 * to calculate distance scores.
 */
FunctionCallInfoBaseData *
CreateFCInfoForScoreCalculation(const SearchQueryEvalData *queryEvalData)
{
	HeapTuple opertup = SearchSysCache1(OPEROID, ObjectIdGetDatum(
											queryEvalData->SimilaritySearchOpOid));
	if (!HeapTupleIsValid(opertup))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"unsupported vector search operator type")));
	}

	Form_pg_operator operform = (Form_pg_operator) GETSTRUCT(opertup);
	Oid funcid = operform->oprcode;
	ReleaseSysCache(opertup);

	FunctionCallInfoBaseData *fcinfo = palloc0(SizeForFunctionCallInfo(2));
	FmgrInfo *flinfo = palloc0(sizeof(FmgrInfo));
	fmgr_info(funcid, flinfo);
	CallContext *callcontext = makeNode(CallContext);
	callcontext->atomic = false;
	InitFunctionCallInfoData(*fcinfo, flinfo, 2 /* args count */,

	                         /*invalidOid*/ 0, (Node *) callcontext, NULL);

	fcinfo->args[1].value = queryEvalData->QueryVector;
	fcinfo->args[0].isnull = false;  /* Assuming the argument is not NULL */
	fcinfo->args[1].isnull = false;

	return fcinfo;
}


/*
 *  Given an input document, this document uses the following information stored in the VectorEvaluationData
 *      1. path that contains the vector. This path is also indexed by the vector index.
 *      2. the query vector
 *      3. the similarity distance function
 *
 *  to extract the vector from the input document and calculate a distance score (aka similarity score)
 *  w.r.t. the query vector.
 */
double
EvaluateMetaSearchScore(pgbson *document)
{
	if (VectorEvaluationData == NULL)
	{
		/*
		 * If VectorEvaluationData is NULL, it means that the execution is outside of the custom scan.
		 * This can happen when vector search with a filter
		 * We get the similarity score from the metadata field __cosmos_meta__.score in the document.
		 * If the metadata field is not available, we throw an error.
		 */
		const char *metaScorePathName =
			VECTOR_METADATA_FIELD_NAME "." VECTOR_METADATA_SCORE_FIELD_NAME;
		bson_iter_t documentIterator;
		if (PgbsonInitIteratorAtPath(document, metaScorePathName, &documentIterator))
		{
			return BsonValueAsDouble(bson_iter_value(&documentIterator));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION40218),
							errmsg(
								"query requires search score metadata, but it is not available")));
		}
	}

	bool isNull = false;
	Datum res = command_bson_extract_vector_base(document,
												 VectorEvaluationData->VectorPathName,
												 &isNull);

	if (isNull)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("Vector field should not be null.")));
	}

	VectorEvaluationData->SimilarityFuncInfo->args[0].value = res;

	Datum result = FunctionCallInvoke(VectorEvaluationData->SimilarityFuncInfo);

	VectorEvaluationData->SimilarityFuncInfo->args[0].value = (Datum) 0;

	/*
	 *  The score calculation logic mimics the distance calculations for
	 *  different distance functions.
	 *  https://github.com/pgvector/pgvector#distances
	 */
	if (VectorEvaluationData->SimilaritySearchOpOid ==
		VectorCosineSimilaritySearchOperatorId())
	{
		return 1.0 - DatumGetFloat8(result);
	}
	else if (VectorEvaluationData->SimilaritySearchOpOid ==
			 VectorIPSimilaritySearchOperatorId())
	{
		return -1.0 * DatumGetFloat8(result);
	}
	else if (VectorEvaluationData->SimilaritySearchOpOid ==
			 VectorL2SimilaritySearchOperatorId())
	{
		return DatumGetFloat8(result);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"unsupported vector search operator type")));
	}
}


/*
 * This function calculates the default number of probes and efSearch for the index.
 * The default nProbes and efSearch are dynamically calculated based on the number of rows in the collection.
 * 1. If the index is IVFFlat:
 *  a. If the number of rows is less than 10K, the default nProbes is the number of lists in the index.
 *  b. If the number of rows is less than 1M, the default nProbes is the number of rows / 1000.
 *  c. If the number of rows is greater than 1M, the default nProbes is sqrt(number of rows).
 * 2. If the index is HNSW:
 *  a. If the number of rows is less than 10K, the default efSearch is the efConstruction in the index.
 *  b. If the number of rows is greater than 10K, the default efSearch is HNSW_DEFAULT_EF_SEARCH.
 */
pgbson *
CalculateSearchParamBsonForIndexPath(IndexPath *vectorSearchPath)
{
	IndexPath *indexPath = (IndexPath *) vectorSearchPath;
	Oid indexRelam = indexPath->indexinfo->relam;
	pgbson *searchParamBson = NULL;

	/* Get rows in the index */
	Cardinality indexRows = indexPath->indexinfo->tuples;
	if (indexRows <= 1)
	{
		indexRows = indexPath->indexinfo->rel->tuples;
	}

	const VectorIndexDefinition *definition = GetVectorIndexDefinitionByIndexAmOid(
		indexRelam);
	if (definition != NULL)
	{
		Oid indexOid = indexPath->indexinfo->indexoid;
		Relation indexRelation = RelationIdGetRelation(indexOid);
		if (indexRelation->rd_options != NULL)
		{
			searchParamBson = definition->calculateSearchParamBsonFunc(
				indexRelation->rd_options, indexRows);
		}

		RelationClose(indexRelation);
	}

	if (searchParamBson == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg(
							"The vector index type is not supported for dynamic calculation of search parameters."),
						errdetail_log(
							"Index type %d does not support dynamic calculation of search parameters",
							indexRelam)));
	}

	return searchParamBson;
}


/*
 * Generates the Index expression for the vector index column
 */
char *
GenerateVectorIndexExprStr(const char *keyPath,
						   const CosmosSearchOptions *searchOptions)
{
	StringInfo indexExprStr = makeStringInfo();

	char *options;
	switch (searchOptions->commonOptions.distanceMetric)
	{
		case VectorIndexDistanceMetric_IPDistance:
		{
			options = "vector_ip_ops";
			break;
		}

		case VectorIndexDistanceMetric_CosineDistance:
		{
			options = "vector_cosine_ops";
			break;
		}

		case VectorIndexDistanceMetric_L2Distance:
		default:
		{
			options = "vector_l2_ops";
			break;
		}
	}

	appendStringInfo(indexExprStr,
					 "CAST(%s.bson_extract_vector(document, %s::text) AS public.vector(%d)) public.%s",
					 ApiCatalogSchemaName, quote_literal_cstr(keyPath),
					 searchOptions->commonOptions.numDimensions,
					 options);
	return indexExprStr->data;
}


/*
 * Checks if a query path matches a vector index and returns the index
 * expression function of the vector index.
 */
bool
IsMatchingVectorIndex(Relation indexRelation, const char *queryVectorPath,
					  FuncExpr **vectorExtractorFunc)
{
	if (indexRelation->rd_index->indnkeyatts != 1)
	{
		/* vector indexes has only one key attributes */
		return false;
	}

	List *indexprs;
	if (indexRelation->rd_indexprs)
	{
		indexprs = indexRelation->rd_indexprs;
	}
	else
	{
		indexprs = RelationGetIndexExpressions(indexRelation);
	}

	/*  rd_index is contains the index information for an Index relation. indkey allows one to access the
	 * indexed colums as an array of column ids. In case of a vector index this is set to 0.*/
	if (indexRelation->rd_index->indkey.values[0] != 0)
	{
		return false;
	}

	if (!IsA(linitial(indexprs), FuncExpr))
	{
		return false;
	}

	FuncExpr *verctorCtrExpr = (FuncExpr *) linitial(indexprs);
	if (verctorCtrExpr->funcid != VectorAsVectorFunctionOid())
	{
		/* Any other index with function expression is not valid vector index */
		return false;
	}

	*vectorExtractorFunc = verctorCtrExpr;
	FuncExpr *vectorSimilarityIndexFuncExpr = (FuncExpr *) linitial(
		verctorCtrExpr->args);                                                 /* First argument */
	Expr *vectorSimilarityIndexPathExpr = (Expr *) lsecond(
		vectorSimilarityIndexFuncExpr->args);
	Assert(IsA(vectorSimilarityIndexPathExpr, Const));
	Const *vectorSimilarityIndexPathConst =
		(Const *) vectorSimilarityIndexPathExpr;

	char *similarityIndexPathName =
		text_to_cstring(DatumGetTextP(vectorSimilarityIndexPathConst->constvalue));

	return queryVectorPath != NULL &&
		   strcmp(queryVectorPath, similarityIndexPathName) == 0;
}


/*
 * Given a vector query path (path that is indexed by a vector index),
 * A predefined "Cast" function that the index uses, and a pointer to the
 * PG index, generates a vector sort Operator that can be pushed down to
 * that specified index.
 */
Expr *
GenerateVectorSortExpr(const char *queryVectorPath,
					   FuncExpr *vectorCastFunc, Relation indexRelation,
					   Node *documentExpr, Node *vectorQuerySpecNode)
{
	Datum queryVectorPathDatum = CStringGetTextDatum(queryVectorPath);
	Const *vectorSimilarityIndexPathConst = makeConst(
		TEXTOID, -1, InvalidOid, -1, queryVectorPathDatum,
		false, false);

	/* ApiCatalogSchemaName.bson_extract_vector(document, 'elem') */
	List *args = list_make2(documentExpr, vectorSimilarityIndexPathConst);
	Expr *vectorExractionFunc = (Expr *) makeFuncExpr(
		ApiCatalogBsonExtractVectorFunctionId(), VectorTypeId(),
		args, InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);

	List *castArgsLeft = list_make3(vectorExractionFunc,
									lsecond(vectorCastFunc->args),
									lthird(vectorCastFunc->args));
	Expr *vectorExractionFuncWithCast = (Expr *) makeFuncExpr(
		vectorCastFunc->funcid, vectorCastFunc->funcresulttype, castArgsLeft,
		InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);

	/* ApiCatalogSchemaName.bson_extract_vector('{ "path" : "myname", "vector": [8.0, 1.0, 9.0], "k": 10 }', 'vector') */
	Datum const_value = CStringGetTextDatum("vector");

	Const *queryText = makeConst(TEXTOID, -1, /*typemod value*/ InvalidOid,
								 -1, /* length of the pointer type*/
								 const_value, false /*constisnull*/,
								 false /* constbyval*/);
	List *queryArgs = list_make2(vectorQuerySpecNode, queryText);
	Expr *vectorExractionFromQueryFunc =
		(Expr *) makeFuncExpr(
			ApiCatalogBsonExtractVectorFunctionId(),
			VectorTypeId(),
			queryArgs,
			InvalidOid, InvalidOid,
			COERCE_EXPLICIT_CALL);

	List *castArgsRight = list_make3(
		vectorExractionFromQueryFunc,
		lsecond(vectorCastFunc->args),
		lthird(vectorCastFunc->args));
	Expr *vectorExractionFromQueryFuncWithCast =
		(Expr *) makeFuncExpr(vectorCastFunc->funcid, vectorCastFunc->funcresulttype,
							  castArgsRight, InvalidOid,
							  InvalidOid,
							  COERCE_EXPLICIT_CALL);

	Oid similaritySearchOpOid = GetSimilarityOperatorOidByFamilyOid(
		indexRelation->rd_opfamily[0], indexRelation->rd_rel->relam);

	OpExpr *opExpr = (OpExpr *) make_opclause(
		similaritySearchOpOid, FLOAT8OID,
		false, vectorExractionFuncWithCast, vectorExractionFromQueryFuncWithCast,
		InvalidOid, InvalidOid);
	return (Expr *) opExpr;
}


/*
 * This function parses the user specified search parameters and set the corresponding GUCs.
 */
void
SetSearchParametersToGUC(Oid vectorAccessMethodOid, pgbson *searchParamBson)
{
	if (searchParamBson == NULL)
	{
		return;
	}

	const VectorIndexDefinition *definition = GetVectorIndexDefinitionByIndexAmOid(
		vectorAccessMethodOid);
	if (definition != NULL)
	{
		definition->setSearchParametersToGUCFunc(searchParamBson);
	}
}


/*
 * This function set the default search parameters to inputState if the user did not specify
 */
void
TrySetDefaultSearchParamForCustomScan(SearchQueryEvalData *querySearchData)
{
	pgbson *searchInput = NULL;
	if (querySearchData->SearchParamBson != (Datum) 0)
	{
		searchInput = DatumGetPgBson(querySearchData->SearchParamBson);
	}

	if (searchInput == NULL || IsPgbsonEmptyDocument(searchInput))
	{
		const VectorIndexDefinition *definition = GetVectorIndexDefinitionByIndexAmOid(
			querySearchData->VectorAccessMethodOid);
		if (definition != NULL)
		{
			querySearchData->SearchParamBson = PointerGetDatum(
				definition->getDefaultSearchParamBsonFunc());
		}
	}
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */

static Oid
GetSimilarityOperatorOidByFamilyOid(Oid operatorFamilyOid, Oid accessMethodOid)
{
	Oid operatorOid = InvalidOid;

	const VectorIndexDefinition *definition = GetVectorIndexDefinitionByIndexAmOid(
		accessMethodOid);

	if (definition != NULL)
	{
		operatorOid = definition->getSimilarityOpOidByFamilyOidFunc(operatorFamilyOid);
	}

	if (operatorOid == InvalidOid)
	{
		const char *accessMethodName = get_am_name(accessMethodOid);
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("Unsupported vector search operator"),
						errdetail_log(
							"Unsupported vector index type: %s, operatorFamilyOid: %u",
							accessMethodName, operatorFamilyOid)));
	}

	return operatorOid;
}
