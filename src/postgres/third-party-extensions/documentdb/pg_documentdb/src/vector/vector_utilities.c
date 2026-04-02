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
#include <catalog/pg_collation.h>
#include <utils/lsyscache.h>

#include "api_hooks.h"
#include "io/bson_core.h"
#include "utils/documentdb_errors.h"
#include "metadata/metadata_cache.h"
#include "vector/bson_extract_vector.h"
#include "vector/vector_common.h"
#include "vector/vector_configs.h"
#include "vector/vector_planner.h"
#include "vector/vector_utilities.h"
#include "vector/vector_spec.h"
#include "utils/error_utils.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static Expr * GenerateVectorExractionExprFromQueryWithCast(Node *vectorQuerySpecNode,
														   FuncExpr *vectorCastFunc);

static VectorIndexDistanceMetric GetDistanceMetricFromOpId(Oid similaritySearchOpId);
static VectorIndexDistanceMetric GetDistanceMetricFromOpName(const
															 char *similaritySearchOpName);

static bool IsHalfVectorCastFunctionCore(FuncExpr *vectorCastFunc,
										 bool logWarning);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40218),
						errmsg(
							"Search score metadata is required for this query, but it is currently unavailable")));
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
CalculateSearchParamBsonForIndexPath(IndexPath *vectorSearchPath, pgbson *searchParamBson)
{
	IndexPath *indexPath = (IndexPath *) vectorSearchPath;
	Oid indexRelam = indexPath->indexinfo->relam;

	/* Read rows from the index*/
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
				indexRelation->rd_options, indexRows, searchParamBson);
		}

		RelationClose(indexRelation);
	}

	if (searchParamBson == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
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
 * e.g.
 *      CAST(API_CATALOG_SCHEMA_NAME.bson_extract_vector(document, 'vect'::text) AS public.vector(2000)) public.vector_l2_ops
 *      CAST(API_CATALOG_SCHEMA_NAME.bson_extract_vector(document, 'vect'::text) AS public.halfvec(4000)) public.halfvec_l2_ops
 */
char *
GenerateVectorIndexExprStr(const char *keyPath,
						   const CosmosSearchOptions *searchOptions)
{
	StringInfo indexExprStr = makeStringInfo();

	char *options;
	char *castVectorType;

	if (searchOptions->commonOptions.compressionType == VectorIndexCompressionType_Half)
	{
		castVectorType = "halfvec";
		switch (searchOptions->commonOptions.distanceMetric)
		{
			case VectorIndexDistanceMetric_IPDistance:
			{
				options = "halfvec_ip_ops";
				break;
			}

			case VectorIndexDistanceMetric_CosineDistance:
			{
				options = "halfvec_cosine_ops";
				break;
			}

			case VectorIndexDistanceMetric_L2Distance:
			default:
			{
				options = "halfvec_l2_ops";
				break;
			}
		}
	}
	else
	{
		/* VectorIndexCompression_None */
		castVectorType = "vector";
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
	}

	appendStringInfo(indexExprStr,
					 "CAST(%s.bson_extract_vector(document, %s::text) AS public.%s(%d)) public.%s",
					 ApiCatalogToApiInternalSchemaName,
					 quote_literal_cstr(keyPath),
					 castVectorType,
					 searchOptions->commonOptions.numDimensions,
					 options);
	return indexExprStr->data;
}


/*
 * Checks if the vector cast function is a cast to half vector.
 */
bool
IsHalfVectorCastFunction(FuncExpr *vectorCastFunc)
{
	return IsHalfVectorCastFunctionCore(vectorCastFunc, false);
}


/*
 * Checks if a query path matches a vector index and returns the index
 * expression function of the vector index.
 */
bool
IsMatchingVectorIndex(Relation indexRelation, const char *queryVectorPath,
					  FuncExpr **vectorCastFunc)
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

	FuncExpr *vectorCtrExpr = (FuncExpr *) linitial(indexprs);
	bool logWarning = true;
	if (vectorCtrExpr->funcid != VectorAsVectorFunctionOid() &&
		!IsHalfVectorCastFunctionCore(vectorCtrExpr, logWarning))
	{
		/* Any other index with function expression is not valid vector index */
		return false;
	}

	/* public.vector(ApiCatalogSchemaName.bson_extract_vector(document, 'v'::text), 2000, true) */
	/* public.vector_to_halfvec(ApiCatalogSchemaName.bson_extract_vector(document, 'v'::text), 4000, true) */
	*vectorCastFunc = vectorCtrExpr;

	/* First argument is extract vector function, ApiCatalogSchemaName.bson_extract_vector(document, 'v'::text) */
	FuncExpr *vectorSimilarityIndexFuncExpr = (FuncExpr *) linitial(
		vectorCtrExpr->args);

	/* Second argument is the vector path */
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
 * e.g.
 *      vector(ApiCatalogSchema.bson_extract_vector(document, 'v_path'), 3, true)
 *      <->
 *      vector(ApiCatalogSchema.bson_extract_vector('{ "vector" : [8.0, 1.0, 9.0], "k" : 2, "path" : "v"}', 'vector'), 3, true)
 */
Expr *
GenerateVectorSortExpr(VectorSearchOptions *vectorSearchOptions,
					   FuncExpr *vectorCastFunc, Relation indexRelation,
					   Node *documentExpr, Node *vectorQuerySpecNode)
{
	const char *queryVectorPath = vectorSearchOptions->searchPath;
	Datum queryVectorPathDatum = CStringGetTextDatum(queryVectorPath);
	Const *vectorSimilarityIndexPathConst = makeConst(
		TEXTOID, -1, DEFAULT_COLLATION_OID, -1, queryVectorPathDatum,
		false, false);

	/* For the exact search, we don't use the vector index
	 * so force the cast function to the full vector if it is a half vector */
	if (vectorSearchOptions->exactSearch &&
		IsHalfVectorCastFunction(vectorCastFunc))
	{
		/* copy the vector cast expr, change the function id to full vector */
		vectorCastFunc = (FuncExpr *) copyObject(vectorCastFunc);
		vectorCastFunc->funcid = VectorAsVectorFunctionOid();
	}

	/* ApiCatalogSchemaName.bson_extract_vector(document, 'elem') */
	List *args = list_make2(documentExpr, vectorSimilarityIndexPathConst);
	Expr *vectorExractionFromDocFunc = (Expr *) makeFuncExpr(
		ApiCatalogBsonExtractVectorFunctionId(), VectorTypeId(),
		args, InvalidOid, DEFAULT_COLLATION_OID, COERCE_EXPLICIT_CALL);

	List *castArgsLeft = list_make3(vectorExractionFromDocFunc,
									lsecond(vectorCastFunc->args),
									lthird(vectorCastFunc->args));
	Expr *vectorExractionFromDocFuncWithCast = (Expr *) makeFuncExpr(
		vectorCastFunc->funcid, vectorCastFunc->funcresulttype, castArgsLeft,
		InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);

	/* To generate the vector extraction function from the query */
	/* ApiCatalogSchemaName.bson_extract_vector('{ "path" : "myname", "vector": [8.0, 1.0, 9.0], "k": 10 }', 'vector') */
	Expr *vectorExractionFromQueryFuncWithCast =
		GenerateVectorExractionExprFromQueryWithCast(
			vectorQuerySpecNode, vectorCastFunc);

	Oid operatorFamilyOid = indexRelation->rd_opfamily[0];

	/* Input type is vector */
	Oid lefttype = indexRelation->rd_opcintype[0];
	Oid righttype = indexRelation->rd_opcintype[0];

	/* The first operator in the vector operator class */
	Oid similaritySearchOpOid = get_opfamily_member(operatorFamilyOid,
													lefttype,
													righttype,
													1);

	vectorSearchOptions->distanceMetric = GetDistanceMetricFromOpId(
		similaritySearchOpOid);
	if (vectorSearchOptions->distanceMetric == VectorIndexDistanceMetric_Unknown)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"unsupported vector search operator type")));
	}

	if (vectorSearchOptions->exactSearch)
	{
		/*
		 * Use the underlying function of the similarity operator
		 * To avoid vector search using vector index
		 * e.g.
		 *      the operator '<=>' with the function 'public.cosine_distance'.
		 *
		 * In exact search, we should always use the full vector function to calculate the distance
		 */
		Oid fullSimilarityOpOid = GetFullVectorOperatorId(
			vectorSearchOptions->distanceMetric);
		Oid similarityFuncOid = get_opcode(fullSimilarityOpOid);

		FuncExpr *funcExpr = (FuncExpr *) makeFuncExpr(
			similarityFuncOid, FLOAT8OID,
			list_make2(vectorExractionFromDocFuncWithCast,
					   vectorExractionFromQueryFuncWithCast),
			InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
		return (Expr *) funcExpr;
	}
	else
	{
		OpExpr *opExpr = (OpExpr *) make_opclause(
			similaritySearchOpOid, FLOAT8OID,
			false, vectorExractionFromDocFuncWithCast,
			vectorExractionFromQueryFuncWithCast,
			InvalidOid, InvalidOid);
		return (Expr *) opExpr;
	}
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
 * This function returns the full vector operator id for the given distance metric.
 */
Oid
GetFullVectorOperatorId(VectorIndexDistanceMetric distanceMetric)
{
	switch (distanceMetric)
	{
		case VectorIndexDistanceMetric_CosineDistance:
		{
			return VectorCosineSimilarityOperatorId();
		}

		case VectorIndexDistanceMetric_IPDistance:
		{
			return VectorIPSimilarityOperatorId();
		}

		case VectorIndexDistanceMetric_L2Distance:
		{
			return VectorL2SimilarityOperatorId();
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Vector search distance type not supported")));
		}
	}

	return InvalidOid;
}


/*
 * The pgvector iterative scan is available from 0.7.0,
 */
bool
IsPgvectorHalfVectorAvailable(void)
{
	/* public.vector_to_halfvec function is introduced in 0.7.0 */
	/* so we can check the function is available or not to indicate the half vector is available */
	bool missingOk = true;
	return OidIsValid(VectorAsHalfVecFunctionOid(missingOk));
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */

/*
 * Generate the expr for extracting the vector from the query spec
 * e.g.
 *      ApiCatalogSchemaName.bson_extract_vector('{ "path" : "myname", "vector": [8.0, 1.0, 9.0], "k": 10 }', 'vector')
 */
static Expr *
GenerateVectorExractionExprFromQueryWithCast(Node *vectorQuerySpecNode,
											 FuncExpr *vectorCastFunc)
{
	/* we extract the vector from the query */
	Datum const_value = CStringGetTextDatum("vector");

	Const *queryText = makeConst(TEXTOID, -1, /*typemod value*/ DEFAULT_COLLATION_OID,
								 -1,     /* length of the pointer type*/
								 const_value, false /*constisnull*/,
								 false /* constbyval*/);
	List *queryArgs = list_make2(vectorQuerySpecNode, queryText);
	Expr *vectorExractionFromQueryFunc =
		(Expr *) makeFuncExpr(
			ApiCatalogBsonExtractVectorFunctionId(),
			VectorTypeId(),
			queryArgs,
			InvalidOid, DEFAULT_COLLATION_OID,
			COERCE_EXPLICIT_CALL);

	List *castArgs = list_make3(
		vectorExractionFromQueryFunc,
		lsecond(vectorCastFunc->args),
		lthird(vectorCastFunc->args));
	Expr *vectorExractionFromQueryFuncWithCast =
		(Expr *) makeFuncExpr(vectorCastFunc->funcid, vectorCastFunc->funcresulttype,
							  castArgs, InvalidOid,
							  DEFAULT_COLLATION_OID,
							  COERCE_EXPLICIT_CALL);

	return vectorExractionFromQueryFuncWithCast;
}


static VectorIndexDistanceMetric
GetDistanceMetricFromOpId(Oid similaritySearchOpId)
{
	const char *similaritySearchOpName = get_opname(similaritySearchOpId);
	return GetDistanceMetricFromOpName(similaritySearchOpName);
}


static VectorIndexDistanceMetric
GetDistanceMetricFromOpName(const char *similaritySearchOpName)
{
	if (similaritySearchOpName == NULL)
	{
		return VectorIndexDistanceMetric_Unknown;
	}

	if (strcmp(similaritySearchOpName, "<->") == 0)
	{
		return VectorIndexDistanceMetric_L2Distance;
	}
	else if (strcmp(similaritySearchOpName, "<=>") == 0)
	{
		return VectorIndexDistanceMetric_CosineDistance;
	}
	else if (strcmp(similaritySearchOpName, "<#>") == 0)
	{
		return VectorIndexDistanceMetric_IPDistance;
	}
	else
	{
		return VectorIndexDistanceMetric_Unknown;
	}
}


/*
 * Checks if the vector cast function is a cast to half vector.
 * This is used to check if the vector index is a half vector index.
 */
static bool
IsHalfVectorCastFunctionCore(FuncExpr *vectorCastFunc, bool logWarning)
{
	if (!IsPgvectorHalfVectorAvailable())
	{
		if (logWarning)
		{
			ereport(WARNING, (errmsg(
								  "The half vector is not supported by pgvector, please check the version of pgvector")));
		}
		return false;
	}

	bool missingOk = false;
	Oid halfVectorCastFuncOid = VectorAsHalfVecFunctionOid(missingOk);

	if (vectorCastFunc != NULL &&
		vectorCastFunc->funcid == halfVectorCastFuncOid)
	{
		return true;
	}

	return false;
}
