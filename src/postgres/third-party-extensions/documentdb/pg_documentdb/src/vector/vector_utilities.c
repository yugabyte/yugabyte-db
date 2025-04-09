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
#include "vector/vector_planner.h"
#include "vector/vector_utilities.h"
#include "vector/vector_spec.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static Expr * GenerateVectorExractionExprFromQueryWithCast(Node *vectorQuerySpecNode,
														   FuncExpr *vectorCastFunc);

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
							"query requires search score metadata, but it is not available")));
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
					 ApiCatalogToApiInternalSchemaName, quote_literal_cstr(keyPath),
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
 * e.g.
 *      vector(ApiCatalogSchema.bson_extract_vector(document, 'v_path'), 3, true)
 *      <->
 *      vector(ApiCatalogSchema.bson_extract_vector('{ "vector" : [8.0, 1.0, 9.0], "k" : 2, "path" : "v"}', 'vector'), 3, true)
 */
Expr *
GenerateVectorSortExpr(const char *queryVectorPath,
					   FuncExpr *vectorCastFunc, Relation indexRelation,
					   Node *documentExpr, Node *vectorQuerySpecNode,
					   bool exactSearch)
{
	Datum queryVectorPathDatum = CStringGetTextDatum(queryVectorPath);
	Const *vectorSimilarityIndexPathConst = makeConst(
		TEXTOID, -1, DEFAULT_COLLATION_OID, -1, queryVectorPathDatum,
		false, false);

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

	if (exactSearch)
	{
		/*
		 * Use the underlying function of the similarity operator
		 * To avoid vector search using vector index
		 * e.g.
		 *      the operator '<=>' with the function 'public.cosine_distance'.
		 */
		Oid similarityFuncOid = get_opcode(similaritySearchOpOid);

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
