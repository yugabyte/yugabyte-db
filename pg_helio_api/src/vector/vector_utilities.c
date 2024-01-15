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
#include <math.h>
#include <miscadmin.h>
#include <fmgr.h>
#include <string.h>
#include <utils/builtins.h>
#include <access/reloptions.h>
#include <tsearch/ts_utils.h>
#include <tsearch/ts_type.h>
#include <tsearch/ts_cache.h>
#include <catalog/namespace.h>
#include <utils/array.h>
#include <nodes/makefuncs.h>
#include <utils/relcache.h>
#include <utils/rel.h>
#include <utils/guc.h>
#include <utils/guc_utils.h>

#include "io/bson_core.h"
#include "opclass/pgmongo_gin_common.h"
#include "opclass/pgmongo_gin_index_mgmt.h"
#include "opclass/pgmongo_bson_gin_private.h"
#include "utils/mongo_errors.h"
#include "opclass/pgmongo_bson_text_gin.h"
#include "metadata/metadata_cache.h"

#include "access/attnum.h"
#include "access/htup.h"
#include "utils/syscache.h"
#include "catalog/pg_type.h"
#include "catalog/pg_operator.h"
#include "utils/lsyscache.h"
#include <vector/vector_utilities.h>
#include "executor/executor.h"
#include "vector/bson_extract_vector.h"
#include "vector/vector_common.h"
#include "vector/vector_planner.h"


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
	int m;                      /* number of connections */
	int efConstruction;         /* size of dynamic candidate list */
} PgVectorHnswOptions;


extern SearchQueryEvalDataWorker *VectorEvaluationData;

PG_FUNCTION_INFO_V1(command_bson_search_param);

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void ParseVectorIndexRelOptions(Oid indexRelam, Relation indexRelation,
									   int *numLists, int *efConstruction);
static void TryParseNumListsAndEFConstruction(Oid indexRelam, Oid relOid, int *numLists,
											  int *efConstruction);

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
		ereport(ERROR, (errcode(MongoBadValue),
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
		ereport(ERROR, (errcode(MongoLocation40218),
						errmsg(
							"query requires search score metadata, but it is not available")));
	}

	bool isNull = false;
	Datum res = command_bson_extract_vector_base(document,
												 VectorEvaluationData->VectorPathName,
												 &isNull);

	if (isNull)
	{
		ereport(ERROR, (errcode(MongoInternalError),
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
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"unsupported vector search operator type")));
	}
}


/*
 * Dummy function used to send search parameters to the workers.
 */
Datum
command_bson_search_param(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(true);
}


/*
 * This function parses the user specified filters and returns the filters that are not
 * 1. mongo_catalog.bson_extract_vector(document, 'v'::text) IS NOT NULL
 * 2. shard_key_value OPERATOR(pg_catalog.=) ::bigint
 * 3. mongo_catalog.bson_search_param(document, '{ "efSearch" : { "$numberInt" : "100" } }'::mongo_catalog.bson))
 *  Example query:
 *		 SELECT document FROM ...
 *		    WHERE
 *				((document OPERATOR(mongo_catalog.#=) '{ "meta.a" : [ { "b" : { "$numberInt" : "3" } } ] }'::mongo_catalog.bson)
 *				AND ((shard_key_value OPERATOR(pg_catalog.=) '4112'::bigint)
 *				AND mongo_catalog.bson_search_param(document, '{ "efSearch" : { "$numberInt" : "100" } }'::mongo_catalog.bson))
 *				AND (mongo_catalog.bson_extract_vector(document, 'v'::text) IS NOT NULL))
 *			ORDER BY (...)
 *			LIMIT ...
 */
void
TryParseUserFilterClause(RelOptInfo *rel, List **userFilters)
{
	ListCell *cell;
	foreach(cell, rel->baserestrictinfo)
	{
		RestrictInfo *rinfo = lfirst(cell);

		if (IsA(rinfo->clause, NullTest))
		{
			/* Skip: mongo_catalog.bson_extract_vector(document, 'v'::text) IS NOT NULL */
			continue;
		}

		if (IsA(rinfo->clause, OpExpr))
		{
			/* Skip: shard_key_value OPERATOR(pg_catalog.=)::bigint */
			OpExpr *opExpr = (OpExpr *) rinfo->clause;
			if (opExpr->opno == BigintEqualOperatorId())
			{
				continue;
			}
		}

		if (IsA(rinfo->clause, FuncExpr))
		{
			/* Skip: mongo_catalog.bson_search_param(document, '{ "efSearch" : { "$numberInt" : "100" } }'::mongo_catalog.bson)) */
			FuncExpr *funcExpr = (FuncExpr *) rinfo->clause;
			if (funcExpr->funcid == ApiBsonSearchParamFunctionId())
			{
				continue;
			}
		}

		/* Filters specified in the search - track them */
		*userFilters = lappend(*userFilters, rinfo->clause);
	}
}


/*
 * This function parses the index options and returns the numLists and efConstruction
 * for the index.
 */
static void
ParseVectorIndexRelOptions(Oid indexRelam, Relation indexRelation, int *numLists,
						   int *efConstruction)
{
	if (indexRelam == PgVectorIvfFlatIndexAmId())
	{
		if (indexRelation->rd_options == NULL)
		{
			*numLists = IVFFLAT_DEFAULT_LISTS;
		}
		else
		{
			PgVectorIvfflatOptions *options =
				(PgVectorIvfflatOptions *) indexRelation->rd_options;
			*numLists = options->lists;
		}
	}
	else if (indexRelam == PgVectorHNSWIndexAmId())
	{
		if (indexRelation->rd_options == NULL)
		{
			*efConstruction = HNSW_DEFAULT_EF_CONSTRUCTION;
		}
		else
		{
			PgVectorHnswOptions *options =
				(PgVectorHnswOptions *) indexRelation->rd_options;
			*efConstruction = options->efConstruction;
		}
	}
}


/*
 * This function parses the index options and returns the default number of probes and efSearch
 * for the index, if the index options are not set, it would not change the input parameters.
 */
static void
TryParseNumListsAndEFConstruction(Oid indexRelam, Oid relOid, int *numLists,
								  int *efConstruction)
{
	Relation indexRelation = RelationIdGetRelation(relOid);
	if (indexRelation->rd_options != NULL)
	{
		ParseVectorIndexRelOptions(indexRelam, indexRelation, numLists, efConstruction);
	}
	RelationClose(indexRelation);
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
void
CalculateDefaultNumProbesAndSearch(IndexPath *vectorSearchPath, double indexRows,
								   int *defaultNumProbes, int *defaultEfSearch)
{
	IndexPath *indexPath = (IndexPath *) vectorSearchPath;

	int numLists = -1;
	int efConstruction = -1;
	Oid indexRelam = indexPath->indexinfo->relam;

	TryParseNumListsAndEFConstruction(
		indexRelam,
		indexPath->indexinfo->indexoid, &numLists, &efConstruction);

	if (indexRelam == PgVectorIvfFlatIndexAmId())
	{
		if (numLists < 0)
		{
			*defaultNumProbes = IVFFLAT_DEFAULT_NPROBES;
		}
		else
		{
			/* nProbes
			 *  < 10000 rows: numLists
			 *  < 1M rows: rows / 1000
			 *  >= 1M rows: sqrt(rows) */
			if (indexRows < VECTOR_SEARCH_SMALL_COLLECTION_ROWS)
			{
				*defaultNumProbes = numLists;
			}
			else if (indexRows < VECTOR_SEARCH_1M_COLLECTION_ROWS)
			{
				*defaultNumProbes = indexRows / 1000;
			}
			else
			{
				*defaultNumProbes = sqrt(indexRows);
			}
		}
	}
	else if (indexRelam == PgVectorHNSWIndexAmId())
	{
		if (efConstruction < 0)
		{
			*defaultEfSearch = HNSW_DEFAULT_EF_SEARCH;
		}
		else
		{
			if (indexRows < VECTOR_SEARCH_SMALL_COLLECTION_ROWS)
			{
				*defaultEfSearch = efConstruction;
			}
			else
			{
				*defaultEfSearch = HNSW_DEFAULT_EF_SEARCH;
			}
		}
	}
}


/*
 * This function parses the user specified search parameters and set the corresponding GUCs.
 */
void
SetSearchParametersToGUC(pgbson *searchParamBson)
{
	if (searchParamBson != NULL)
	{
		bson_iter_t documentIterator;
		PgbsonInitIterator(searchParamBson, &documentIterator);
		while (bson_iter_next(&documentIterator))
		{
			const char *key = bson_iter_key(&documentIterator);
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
			else if (strcmp(key, VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH) == 0)
			{
				int32_t efSearch = BsonValueAsInt32(bson_iter_value(
														&documentIterator));

				/*
				 * set efSearch to local GUC hnsw.ef_search
				 */
				char efSearchStr[20];
				snprintf(efSearchStr, sizeof(efSearchStr), "%d",
						 efSearch);
				SetGUCLocally("hnsw.ef_search", efSearchStr);
			}
		}
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
		pgbson_writer optionsWriter;
		PgbsonWriterInit(&optionsWriter);

		if (querySearchData->VectorAccessMethodOid == PgVectorIvfFlatIndexAmId())
		{
			PgbsonWriterAppendInt32(&optionsWriter, VECTOR_PARAMETER_NAME_IVF_NPROBES,
									VECTOR_PARAMETER_NAME_IVF_NPROBES_STR_LEN,
									IVFFLAT_DEFAULT_NPROBES);
		}
		else if (querySearchData->VectorAccessMethodOid == PgVectorHNSWIndexAmId())
		{
			PgbsonWriterAppendInt32(&optionsWriter,
									VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH,
									VECTOR_PARAMETER_NAME_HNSW_EF_SEARCH_STR_LEN,
									HNSW_DEFAULT_EF_SEARCH);
		}

		querySearchData->SearchParamBson = PointerGetDatum(
			PgbsonWriterGetPgbson(&optionsWriter));
	}
}
