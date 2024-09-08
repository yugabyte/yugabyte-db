/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/bson_aggregation_output_pipeline.c
 *
 * Implementation of the backend query generation for output pipelines that have (such as $out, $merge).
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <float.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <utils/lsyscache.h>
#include <access/xact.h>
#include <catalog/pg_operator.h>
#include <optimizer/planner.h>
#include <nodes/nodes.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <parser/parser.h>
#include <parser/parse_relation.h>
#include <parser/parse_agg.h>
#include <parser/parse_clause.h>
#include <parser/parse_param.h>
#include <parser/analyze.h>
#include <parser/parse_oper.h>
#include <utils/ruleutils.h>
#include <utils/builtins.h>
#include <catalog/pg_aggregate.h>
#include <catalog/pg_class.h>
#include <catalog/namespace.h>
#include <rewrite/rewriteSearchCycle.h>
#include <utils/version_utils.h>

#include "io/helio_bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "query/helio_bson_compare.h"
#include "planner/helio_planner.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "commands/insert.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "utils/feature_counter.h"
#include "operators/bson_expression.h"
#include "metadata/index.h"
#include "utils/hashset_utils.h"
#include "aggregation/bson_tree.h"
#include "aggregation/bson_tree_write.h"
#include "optimizer/optimizer.h"

#include "aggregation/bson_aggregation_pipeline_private.h"

/*
 * $merge stage input field `WhenMatched` options
 */
typedef enum WhenMatchedAction
{
	WhenMatched_REPLACE = 0,
	WhenMatched_KEEPEXISTING = 1,
	WhenMatched_MERGE = 2,
	WhenMatched_FAIL = 3,
	WhenMatched_PIPELINE = 4,
	WhenMatched_LET = 5
} WhenMatchedAction;

/*
 * $merge stage input field `WhenNotMatched` options
 */
typedef enum WhenNotMatchedAction
{
	WhenNotMatched_INSERT = 0,
	WhenNotMatched_DISCARD = 1,
	WhenNotMatched_FAIL = 2,
} WhenNotMatchedAction;

/*
 * Struct having parsed view of the
 * arguments to $merge stage.
 */
typedef struct MergeArgs
{
	/* name of input target Databse */
	StringView targetDB;

	/* name of input target collection */
	StringView targetCollection;

	/* input `on` field can be an array or string */
	bson_value_t on;

	/* input `whenMatched` field */
	WhenMatchedAction whenMatched;

	/* input `whenNotMatched` field */
	WhenNotMatchedAction whenNotMatched;
} MergeArgs;

/* GUC to enable $merge aggregation stage */
extern bool EnableMergeStage;

/* GUC to enable $merge target collection creatation if not exist */
extern bool EnableMergeTargetCreation;

/* GUC to enable $merge across databases */
extern bool EnableMergeAcrossDB;

static void ParseMergeStage(const bson_value_t *existingValue, const
							char *currentNameSpace, MergeArgs *args);
static void VaildateMergeOnFieldValues(const bson_value_t *onArray, uint64
									   collectionId);
static void RearrangeTargetListForMerge(Query *query, MongoCollection *targetCollection,
										bool isSourceAndTargetAreSame, const
										bson_value_t *onFields);
static void WriteJoinConditionToQueryDollarMerge(Query *query,
												 Var *sourceDocVar,
												 Var *targetDocVar,
												 Var *sourceShardKeyValueVar,
												 Var *targetShardKeyValueVar,
												 Var *targetObjectIdVar,
												 const int
												 sourceExtractedOnFieldsInitIndex,
												 const int sourceCollectionVarNo,
												 MergeArgs mergeArgs);
static MergeAction * MakeActionWhenMatched(WhenMatchedAction whenMatched,
										   Var *sourceDocVar,
										   Var *targetDocVar);
static MergeAction * MakeActionWhenNotMatched(WhenNotMatchedAction whenNotMatched,
											  Var *sourceDocVar,
											  Var *generatedObjectIdVar,
											  Var *sourceShardKeyVar,
											  MongoCollection *targetCollection);
static bool IsCompoundUniqueIndexPresent(const bson_value_t *onValues,
										 bson_iter_t *indexKeyDocumnetIter,
										 const int totalIndexKeys);
static void ValidateAndAddObjectIdToWriter(pgbson_writer *writer,
										   pgbson *sourceDocument,
										   pgbson *targetDocument);
static inline bool IsSingleUniqueIndexPresent(const char *onValue,
											  bson_iter_t *indexKeyDocumnetIter);
static inline void AddTargetCollectionRTEDollarMerge(Query *query,
													 MongoCollection *targetCollection);
static HTAB * InitHashTableFromStringArray(const bson_value_t *onValues, int
										   onValuesArraySize);
static inline bool ValidatePreviousStagesOfDollarMerge(Query *query);
static bool MergeQueryCTEWalker(Node *node, void *context);
static inline void ValidateFinalPgbsonBeforeWriting(const pgbson *sourceDocument);
static inline Expr * CreateSingleJoinExpr(const char *joinField,
										  Var *sourceDocVar,
										  Var *targetDocVar,
										  Var *targetObjectIdVar,
										  const int extractFieldResNumber,
										  const int sourceCollectionVarNo);
static inline TargetEntry * MakeExtractFuncExprForMergeTE(const char *onField, uint32
														  length, Var *sourceDocument,
														  const int resNum);
static void WriteJoinConditionToQueryDollarMergeLegacy(Query *query, Var *sourceDocVar,
													   Var *targetDocVar,
													   Var *sourceShardKeyValueVar,
													   Var *targetShardKeyValueVar,
													   MergeArgs mergeArgs);

PG_FUNCTION_INFO_V1(bson_dollar_merge_handle_when_matched);
PG_FUNCTION_INFO_V1(bson_dollar_merge_add_object_id);
PG_FUNCTION_INFO_V1(bson_dollar_merge_fail_when_not_matched);
PG_FUNCTION_INFO_V1(bson_dollar_merge_generate_object_id);
PG_FUNCTION_INFO_V1(bson_dollar_extract_merge_filter);


/*
 * This function extracts merge filter from source document to match against target document.
 */
Datum
bson_dollar_extract_merge_filter(PG_FUNCTION_ARGS)
{
	pgbson *sourceDocument = PG_GETARG_PGBSON_PACKED(0);
	char *joinField = text_to_cstring(PG_GETARG_TEXT_P(1));

	bson_iter_t sourceIter;
	if (!PgbsonInitIteratorAtPath(sourceDocument, joinField, &sourceIter))
	{
		/* If the source lacks an object ID, we return false and generate a new one during the document's insertion into the target. */
		/* when it come's to join filter for _id field we create target.objectid = bson_get_value(agg_stage_1.document, '_id'::text)*/
		if (strcmp(joinField, "_id") == 0)
		{
			PG_RETURN_NULL();
		}

		ereport(ERROR, (errcode(MongoLocation51132),
						errmsg(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array"),
						errhint(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array")));
	}

	pgbsonelement filterElement;
	filterElement.path = joinField;
	filterElement.pathLength = strlen(joinField);
	filterElement.bsonValue = *bson_iter_value(&sourceIter);

	if (filterElement.bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation51185),
						errmsg(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array"),
						errhint(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array")));
	}
	else if (filterElement.bsonValue.value_type == BSON_TYPE_NULL ||
			 filterElement.bsonValue.value_type == BSON_TYPE_UNDEFINED)
	{
		ereport(ERROR, (errcode(MongoLocation51132),
						errmsg(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array"),
						errhint(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array")));
	}

	PG_RETURN_POINTER(PgbsonElementToPgbson(&filterElement));
}


/*
 * In the `$merge` stage, this function is utilized to add the '_id' field to the source document if it is missing.
 * Stages such as $project have the potential to eliminate the _id field, which is essential for inserting into the target collection.
 */
Datum
bson_dollar_merge_add_object_id(PG_FUNCTION_ARGS)
{
	pgbson *sourceDocument = PG_GETARG_PGBSON_PACKED(0);
	pgbson *generatedObjectID = PG_GETARG_PGBSON(1);

	/* Add and validate _id */
	pgbson *outputBson = RewriteDocumentWithCustomObjectId(sourceDocument,
														   generatedObjectID);
	ValidateFinalPgbsonBeforeWriting(outputBson);

	/* Free only when outputBson is different from sourceDocument*/
	if (sourceDocument != outputBson)
	{
		PG_FREE_IF_COPY(sourceDocument, 0);
	}

	PG_RETURN_POINTER(outputBson);
}


/*
 * In the `$merge` stage, this function is utilized to generate object id field.
 * we use generated object id in case source document does not have object id.
 */
Datum
bson_dollar_merge_generate_object_id(PG_FUNCTION_ARGS)
{
	PG_RETURN_POINTER(PgbsonGenerateOidDocument());
}


/*
 * In the `$merge` stage, this function is utilized to handle the `whenMatched` actions of the `$merge` stage.
 */
Datum
bson_dollar_merge_handle_when_matched(PG_FUNCTION_ARGS)
{
	pgbson *sourceDocument = PG_GETARG_PGBSON(0);
	pgbson *targetDocument = PG_GETARG_PGBSON(1);

	WhenMatchedAction action = PG_GETARG_INT32(2);
	pgbson *finalDocument = NULL;

	switch (action)
	{
		case WhenMatched_REPLACE:
		{
			pgbson_writer writer;
			ValidateAndAddObjectIdToWriter(&writer, sourceDocument, targetDocument);

			bson_iter_t sourceDocumentIterator;
			PgbsonInitIterator(sourceDocument, &sourceDocumentIterator);

			while (bson_iter_next(&sourceDocumentIterator))
			{
				const char *key = bson_iter_key(&sourceDocumentIterator);

				/* ensure we're not rewriting the _id to something else. */
				if (strcmp(key, "_id") == 0)
				{
					continue;
				}

				uint32_t keyLength = bson_iter_key_len(&sourceDocumentIterator);
				PgbsonWriterAppendValue(&writer, key, keyLength, bson_iter_value(
											&sourceDocumentIterator));
			}

			finalDocument = PgbsonWriterGetPgbson(&writer);
			break;
		}

		case WhenMatched_MERGE:
		{
			pgbson_writer writer;
			ValidateAndAddObjectIdToWriter(&writer, sourceDocument, targetDocument);

			/*
			 * Project the key-value pairs of both the source and target documents onto the BsonIntermediatePathNode Tree.
			 * The source document is projected first because its values are prioritized; if a target document's key already exists in the tree,
			 * its values will not override the existing ones.
			 */
			BsonIntermediatePathNode *tree = MakeRootNode();
			ParseAggregationExpressionContext parseContext = { 0 };
			BuildTreeFromPgbson(tree, sourceDocument, &parseContext);
			BuildTreeFromPgbson(tree, targetDocument, &parseContext);

			TraverseTreeAndWrite(tree, &writer, targetDocument);
			finalDocument = PgbsonWriterGetPgbson(&writer);

			FreeTree(tree);
			break;
		}

		case WhenMatched_KEEPEXISTING:
		{
			/* we are not suppose to reach here if action is `WhenMatched_KEEPEXISTING` we should set `DO NOTHING` Action of PG */
			ereport(ERROR, errcode(MongoInternalError), (errmsg(
															 "whenMathed KeepEXISTING should not reach here"),
														 errhint(
															 "whenMathed KeepEXISTING should not reach here")));
		}

		case WhenMatched_FAIL:
		{
			ereport(ERROR, (errcode(MongoDuplicateKey),
							errmsg(
								"$merge with whenMatched: fail found an existing document with the same values for the 'on' fields"),
							errhint(
								"$merge with whenMatched: fail found an existing document with the same values for the 'on' fields")));
		}

		case WhenMatched_PIPELINE:
		case WhenMatched_LET:
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg(
								"merge, pipeline and Let option not supported yet in whenMatched field of $merge aggreagtion stage"),
							errhint(
								"merge, pipeline and Let option not supported yet in whenMatched field of $merge aggreagtion stage")));
		}

		default:
		{
			ereport(ERROR, errcode(MongoInternalError), (errmsg(
															 "Unrecognized WhenMatched value"),
														 errhint(
															 "Unrecognized WhenMatched value")));
		}
	}

	/* let's validate final document before writing */
	ValidateFinalPgbsonBeforeWriting(finalDocument);
	PG_RETURN_POINTER(finalDocument);
}


/*
 * In the `$merge` stage, to handle `fail` action of `WhenNotMatched` case.
 * This function accepts dummy arguments and has return type to prevent PostgreSQL from treating it as a constant function and evaluating it prematurely.
 */
Datum
bson_dollar_merge_fail_when_not_matched(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(MongoMergeStageNoMatchingDocument),
					errmsg(
						"$merge could not find a matching document in the target collection for at least one document in the source collection"),
					errhint(
						"$merge could not find a matching document in the target collection for at least one document in the source collection")));

	PG_RETURN_NULL();
}


/*
 * Mutates the query for the $merge stage
 *
 * Example mongo command : { $merge: { into: "targetCollection", on: "_id", whenMatched: "replace", whenNotMatched: "insert" } }
 * sql query :
 *
 * MERGE INTO ONLY mongo_data.documents_2 documents_2
 * USING (
 *          SELECT collection.document AS document,
 *                 '2'::bigint AS target_shard_key_value,  -- (2 is collection_id of target collection)
 *                  bson_dollar_merge_generate_object_id(collection.document) AS generated_object_id
 *			FROM mongo_data.documents_1 collection
 *			WHERE collection.shard_key_value = '1'::bigint
 *		 ) agg_stage_0
 * ON documents_2.shard_key_value OPERATOR(pg_catalog.=) agg_stage_0.target_shard_key_value
 * AND bson_dollar_merge_join(documents_2.document, agg_stage_0.document, '_id'::text)
 * WHEN MATCHED
 * THEN
 *      UPDATE SET document = bson_dollar_merge_handle_when_matched(agg_stage_0.document, documents_2.document, 1)
 * WHEN NOT MATCHED
 * THEN
 *      INSERT (shard_key_value, object_id, document, creation_time)
 *      VALUES (agg_stage_0.target_shard_key_value, bson_get_value((agg_stage_0.document).document, '_id'::text), (agg_stage_0.document).document, '2024-05-28 04:01:26.360522+00'::timestamp with time zone);
 *
 */
Query *
HandleMerge(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_MERGE);

	if (IsCollationApplicable(context->collationString))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported), errmsg(
							"collation is not supported with $merge yet")));
	}

	if (!(IsClusterVersionAtleastThis(1, 19, 0) && EnableMergeStage))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg("Stage $merge is not supported yet in native pipeline"),
						errhint("Stage $merge is not supported yet in native pipeline")));
	}

	bool isTopLevel = true;
	if (IsInTransactionBlock(isTopLevel))
	{
		ereport(ERROR, (errcode(MongoOperationNotSupportedInTransaction),
						errmsg("$merge cannot be used in a transaction")));
	}

	/* if source table does not exist do not modify query */
	if (context->mongoCollection == NULL)
	{
		return query;
	}

	ValidatePreviousStagesOfDollarMerge(query);

	MergeArgs mergeArgs;
	memset(&mergeArgs, 0, sizeof(mergeArgs));
	ParseMergeStage(existingValue, context->namespaceName, &mergeArgs);

	/* Look for target collection details */
	Datum databaseNameDatum = StringViewGetTextDatum(&mergeArgs.targetDB);
	Datum collectionNameDatum = StringViewGetTextDatum(&mergeArgs.targetCollection);

	MongoCollection *targetCollection = GetMongoCollectionOrViewByNameDatum(
		databaseNameDatum,
		collectionNameDatum,
		RowExclusiveLock);

	/* if target collection not exist create one */
	if (targetCollection == NULL)
	{
		/* Currently, if a collection is created and a subsequent query fails, we don't create a table, but the collection_id still increments, which is not the desired behavior.
		 * To pass JS tests, we are temporarily keeping EnableMergeTargetCreation as true. However, this will be disabled in the production environment.
		 * TODO: We need to devise a strategy to prevent the increment of collection_id if a query fails after the creation of a collection.
		 */
		if (EnableMergeTargetCreation)
		{
			int ignoreCollectionID = 0;
			VaildateMergeOnFieldValues(&mergeArgs.on, ignoreCollectionID);
			targetCollection = CreateCollectionForInsert(databaseNameDatum,
														 collectionNameDatum);
		}
		else
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg(
								"$merge target collection create not supported yet, Please create target collection first and try again"),
							errhint(
								"$merge target collection create not supported yet, Please create target collection first and try again")));
		}
	}
	else
	{
		if (targetCollection->viewDefinition != NULL)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupportedOnView),
							errmsg("Namespace %s.%s is a view, not a collection",
								   targetCollection->name.databaseName,
								   targetCollection->name.collectionName),
							errhint("Namespace %s.%s is a view, not a collection",
									targetCollection->name.databaseName,
									targetCollection->name.collectionName)));
		}
		else if (targetCollection->shardKey != NULL)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg(
								"$merge for sharded output collection not supported yet"),
							errhint(
								"$merge for sharded output collection not supported yet")));
		}

		VaildateMergeOnFieldValues(&mergeArgs.on, targetCollection->collectionId);
	}

	bool isSourceAndTargetAreSame = (targetCollection->collectionId ==
									 context->mongoCollection->collectionId);

	/* constant for target collection */
	const int targetCollectionVarNo = 1; /* In merge query target table is 1st table */
	const int targetShardKeyValueAttrNo = 1; /* From Target table we are just selecting 3 columns first one is shard_key_value */
	const int targetObjectIdAttrNo = 2; /* From Target table we are just selecting 3 columns first one is shard_key_value */
	const int targetDocAttrNo = 3; /* From Target table we are just selecting 3 columns third one is document */

	/* constant for source collection */
	const int sourceCollectionVarNo = 2; /* In merge query source table is 2nd table */
	const int sourceDocAttrNo = 1; /* In source table first projector is document */
	const int sourceShardKeyValueAttrNo = 2; /* we will append shard_key_value in source query at 2nd position after document column */
	const int generatedObjectIdAttrNo = 3;  /* we will append generated object_id in source query at 3rd position after shard_key_value column */
	const int sourceExtractedOnFieldsInitIndex = 4; /* We append all extracted source TEs starting from index 4 and repeat this process for all 'on' fields. */

	if (targetCollection->shardKey == NULL)
	{
		RearrangeTargetListForMerge(query, targetCollection,
									isSourceAndTargetAreSame,
									&mergeArgs.on);
	}

	context->expandTargetList = true;
	query = MigrateQueryToSubQuery(query, context);
	query->commandType = CMD_MERGE;
	AddTargetCollectionRTEDollarMerge(query, targetCollection);

	Var *sourceDocVar = makeVar(sourceCollectionVarNo, sourceDocAttrNo,
								BsonTypeId(), -1,
								InvalidOid, 0);
	Var *targetObjectIdVar = makeVar(targetCollectionVarNo, targetObjectIdAttrNo,
									 BsonTypeId(), -1,
									 InvalidOid, 0);
	Var *targetDocVar = makeVar(targetCollectionVarNo, targetDocAttrNo, BsonTypeId(), -1,
								InvalidOid, 0);
	Var *targetShardKeyValueVar = makeVar(targetCollectionVarNo,
										  targetShardKeyValueAttrNo, INT8OID, -1, 0, 0);
	Var *sourceShardKeyValueVar = makeVar(sourceCollectionVarNo,
										  sourceShardKeyValueAttrNo, INT8OID, -1, 0, 0);
	Var *generatedObjectIdVar = makeVar(sourceCollectionVarNo,
										generatedObjectIdAttrNo, BsonTypeId(), -1, 0, 0);

	query->mergeActionList = list_make2(MakeActionWhenMatched(mergeArgs.whenMatched,
															  sourceDocVar, targetDocVar),
										MakeActionWhenNotMatched(mergeArgs.whenNotMatched,
																 sourceDocVar,
																 generatedObjectIdVar,
																 sourceShardKeyValueVar,
																 targetCollection));

	if (IsClusterVersionAtleastThis(1, 20, 0))
	{
		WriteJoinConditionToQueryDollarMerge(query, sourceDocVar, targetDocVar,
											 sourceShardKeyValueVar,
											 targetShardKeyValueVar,
											 targetObjectIdVar,
											 sourceExtractedOnFieldsInitIndex,
											 sourceCollectionVarNo,
											 mergeArgs);
	}
	else
	{
		WriteJoinConditionToQueryDollarMergeLegacy(query, sourceDocVar,
												   targetDocVar,
												   sourceShardKeyValueVar,
												   targetShardKeyValueVar, mergeArgs);
	}

	return query;
}


/*
 * create MergeAction for `whenMatched` case.
 * This function is responsible for constructing the following segment of the merge query :
 * WHEN MATCHED THEN
 * UPDATE SET document = bson_dollar_merge_handle_when_matched(agg_stage_4.document, documents_1.document, 0)
 */
static MergeAction *
MakeActionWhenMatched(WhenMatchedAction whenMatched, Var *sourceDocVar, Var *targetDocVar)
{
	MergeAction *action = makeNode(MergeAction);
	action->matched = true;

	if (whenMatched == WhenMatched_KEEPEXISTING)
	{
		action->commandType = CMD_NOTHING;
		return action;
	}

	action->commandType = CMD_UPDATE;
	Const *inputActionForWhenMathced = makeConst(INT4OID, -1, InvalidOid, sizeof(int32),
												 Int32GetDatum(whenMatched),
												 false, true);

	List *args = list_make3(sourceDocVar, targetDocVar, inputActionForWhenMathced);
	FuncExpr *resultExpr = makeFuncExpr(
		BsonDollarMergeHandleWhenMatchedFunctionOid(), BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	action->targetList = list_make1(
		makeTargetEntry((Expr *) resultExpr,
						MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER, "document", false)
		);
	return action;
}


/*
 * create MergeAction for `whenNotMatched` case
 * This function is responsible for constructing the following segment of the merge query :
 * WHEN NOT MATCHED THEN
 * INSERT (shard_key_value, object_id, document, creation_time)
 * VALUE (source.target_shard_key_value,
 *        COALESCE(bson_get_value(source.document, '_id'::text),
 *        source.document), bson_dollar_merge_add_object_id(source.document),
 *        <current-time>)
 */
static MergeAction *
MakeActionWhenNotMatched(WhenNotMatchedAction whenNotMatched, Var *sourceDocVar,
						 Var *generatedObjectIdVar,
						 Var *sourceShardKeyVar, MongoCollection *targetCollection)
{
	MergeAction *action = makeNode(MergeAction);
	action->matched = false;

	if (whenNotMatched == WhenNotMatched_DISCARD)
	{
		action->commandType = CMD_NOTHING;
		return action;
	}

	action->commandType = CMD_INSERT;
	TimestampTz nowValueTime = GetCurrentTimestamp();
	Const *nowValue = makeConst(TIMESTAMPTZOID, -1, InvalidOid, 8,
								TimestampTzGetDatum(nowValueTime), false, true);

	/* let's build func expr for `object_id` column */
	const char *objectIdField = "_id";
	StringView objectIdFieldStringView = CreateStringViewFromString(objectIdField);
	Const *objectIdConst = MakeTextConst(objectIdFieldStringView.string,
										 objectIdFieldStringView.length);

	List *argsBsonGetValueFunc = list_make2(sourceDocVar, objectIdConst);
	Oid functionOid = (whenNotMatched == WhenNotMatched_INSERT) ?
					  BsonGetValueFunctionOid() :
					  BsonDollarMergeFailWhenNotMatchedFunctionOid();

	FuncExpr *bsonGetValueFuncExpr = makeFuncExpr(
		functionOid, BsonTypeId(), argsBsonGetValueFunc, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	CoalesceExpr *coalesce = makeNode(CoalesceExpr);
	coalesce->coalescetype = BsonTypeId();
	coalesce->coalescecollid = InvalidOid;
	coalesce->args = list_make2(bsonGetValueFuncExpr, generatedObjectIdVar);

	/* let's build func expr for `document` column */
	List *argsForAddObjecIdFunc = list_make2(sourceDocVar, generatedObjectIdVar);
	FuncExpr *addObjecIdFuncExpr = makeFuncExpr(
		BsonDollarMergeAddObjectIdFunctionOid(), BsonTypeId(), argsForAddObjecIdFunc,
		InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	/* for insert operation */
	action->targetList = list_make4(
		makeTargetEntry((Expr *) sourceShardKeyVar,
						MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER,
						"target_shard_key_value", false),
		makeTargetEntry((Expr *) coalesce,
						MONGO_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER, "object_id", false),
		makeTargetEntry((Expr *) addObjecIdFuncExpr,
						MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER, "document", false),
		makeTargetEntry((Expr *) nowValue,
						targetCollection->mongoDataCreationTimeVarAttrNumber,
						"creation_time",
						false)
		);

	return action;
}


/*
 * Parses & validates the input $merge spec.
 *
 * { $merge: {
 *     into: <collection> -or- { db: <db>, coll: <collection> },
 *     on: <identifier field> -or- [ <identifier field1>, ...],  // Optional
 *     let: <variables>,                                         // Optional
 *     whenMatched: <replace|keepExisting|merge|fail|pipeline>,  // Optional
 *    whenNotMatched: <insert|discard|fail>                     // Optional
 * } }
 *
 * Parsed outputs are placed in the MergeArgs struct.
 */
static void
ParseMergeStage(const bson_value_t *existingValue, const char *currentNameSpace,
				MergeArgs *args)
{
	if (existingValue->value_type != BSON_TYPE_DOCUMENT && existingValue->value_type !=
		BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(MongoTypeMismatch),
						errmsg(
							"$merge requires a string or object argument, but found %s",
							BsonTypeName(
								existingValue->value_type)),
						errhint(
							"$merge requires a string or object argument, but found %s",
							BsonTypeName(
								existingValue->value_type))));
	}

	if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		args->targetCollection = (StringView) {
			.length = existingValue->value.v_utf8.len,
			.string = existingValue->value.v_utf8.str
		};

		args->on.value_type = BSON_TYPE_UTF8;
		args->on.value.v_utf8.len = 3;
		args->on.value.v_utf8.str = "_id";

		StringView currentNameSpaceView = CreateStringViewFromString(currentNameSpace);
		args->targetDB = StringViewFindPrefix(&currentNameSpaceView, '.');
		return;
	}

	/* parse when input is a document */
	bson_iter_t mergeIter;
	BsonValueInitIterator(existingValue, &mergeIter);
	bool isOnSpecified = false;

	while (bson_iter_next(&mergeIter))
	{
		const char *key = bson_iter_key(&mergeIter);
		const bson_value_t *value = bson_iter_value(&mergeIter);
		if (strcmp(key, "into") == 0)
		{
			if (value->value_type == BSON_TYPE_UTF8)
			{
				args->targetCollection = (StringView) {
					.length = value->value.v_utf8.len,
					.string = value->value.v_utf8.str
				};
			}
			else if (value->value_type == BSON_TYPE_DOCUMENT)
			{
				bson_iter_t intoIter;
				BsonValueInitIterator(value, &intoIter);

				while (bson_iter_next(&intoIter))
				{
					const char *innerKey = bson_iter_key(&intoIter);
					const bson_value_t *innerValue = bson_iter_value(&intoIter);

					if (innerValue->value_type != BSON_TYPE_UTF8)
					{
						ereport(ERROR, (errcode(MongoFailedToParse),
										errmsg(
											"BSON field 'into.%s' is the wrong type '%s', expected type 'string",
											innerKey, BsonTypeName(value->value_type)),
										errhint(
											"BSON field 'into.%s' is the wrong type '%s', expected type 'string",
											innerKey, BsonTypeName(value->value_type))));
					}

					if (strcmp(innerKey, "db") == 0)
					{
						args->targetDB = (StringView) {
							.length = innerValue->value.v_utf8.len,
							.string = innerValue->value.v_utf8.str
						};
					}
					else if (strcmp(innerKey, "coll") == 0)
					{
						args->targetCollection = (StringView) {
							.length = innerValue->value.v_utf8.len,
							.string = innerValue->value.v_utf8.str
						};
					}
					else
					{
						ereport(ERROR, (errcode(MongoUnknownBsonField),
										errmsg("BSON field 'into.%s' is an unknown field",
											   innerKey),
										errhint(
											"BSON field 'into.%s' is an unknown field",
											innerKey)));
					}
				}

				if (args->targetCollection.length == 0)
				{
					ereport(ERROR, (errcode(MongoLocation51178),
									errmsg(
										"$merge 'into' field must specify a 'coll' that is not empty, null or undefined"),
									errhint(
										"$merge 'into' field must specify a 'coll' that is not empty, null or undefined")));
				}
			}
			else
			{
				ereport(ERROR, (errcode(MongoLocation51178),
								errmsg(
									"$merge 'into' field  must be either a string or an object, but found %s",
									BsonTypeName(value->value_type)),
								errhint(
									"$merge 'into' field  must be either a string or an object, but found %s",
									BsonTypeName(value->value_type))));
			}

			StringView nameSpaceView = CreateStringViewFromString(currentNameSpace);
			StringView currentDBName = StringViewFindPrefix(&nameSpaceView, '.');

			/* if target database name not mentioned in input let's use source database */
			if (args->targetDB.length == 0)
			{
				args->targetDB = currentDBName;
			}
			else if (!EnableMergeAcrossDB && !StringViewEquals(&currentDBName,
															   &args->targetDB))
			{
				ereport(ERROR, (errcode(MongoCommandNotSupported),
								errmsg("merge is not supported across databases")));
			}
		}
		else if (strcmp(key, "on") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8 && value->value_type !=
				BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (errcode(MongoLocation51186),
								errmsg(
									"$merge 'on' field  must be either a string or an array of strings, but found %s",
									BsonTypeName(value->value_type)),
								errhint(
									"$merge 'on' field  must be either a string or an array of strings, but found %s",
									BsonTypeName(value->value_type))));
			}

			/* let's verify in parsing phase itself that values inside on array are of type string only and fail early if needed */
			if (value->value_type == BSON_TYPE_ARRAY)
			{
				bson_iter_t onValuesIter;
				BsonValueInitIterator(value, &onValuesIter);
				bool atLeastOneElement = false;
				while (bson_iter_next(&onValuesIter))
				{
					atLeastOneElement = true;
					const bson_value_t *onValuesElement = bson_iter_value(&onValuesIter);
					if (onValuesElement->value_type != BSON_TYPE_UTF8)
					{
						ereport(ERROR, (errcode(MongoLocation51134),
										errmsg(
											"$merge 'on' array elements must be strings, but found %s",
											BsonTypeName(onValuesElement->value_type)),
										errhint(
											"$merge 'on' array elements must be strings, but found %s",
											BsonTypeName(onValuesElement->value_type))));
					}
				}

				if (!atLeastOneElement)
				{
					ereport(ERROR, (errcode(MongoLocation51187),
									errmsg(
										"If explicitly specifying $merge 'on', must include at least one field"),
									errhint(
										"If explicitly specifying $merge 'on', must include at least one field")));
				}
			}

			args->on = *value;
			isOnSpecified = true;
		}
		else if (strcmp(key, "let") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("let option is not supported"),
							errhint("let option is not supported")));
		}
		else if (strcmp(key, "whenMatched") == 0)
		{
			if (value->value_type == BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (errcode(MongoCommandNotSupported),
								errmsg(
									"$merge 'whenMatched' with 'pipeline' not supported yet"),
								errhint(
									"$merge 'whenMatched' with 'pipeline' not supported yet")));
			}
			else if (value->value_type != BSON_TYPE_UTF8)
			{
				/* TODO : Modify error text when we support pipeline. Replace `must be string` with `must be either a string or array` */
				ereport(ERROR, (errcode(MongoLocation51191),
								errmsg(
									"$merge 'whenMatched' field  must be string, but found %s",
									BsonTypeName(
										value->value_type)),
								errhint(
									"$merge 'whenMatched' field  must be string, but found %s",
									BsonTypeName(
										value->value_type))));
			}


			if (strcmp(value->value.v_utf8.str, "replace") == 0)
			{
				args->whenMatched = WhenMatched_REPLACE;
			}
			else if (strcmp(value->value.v_utf8.str, "keepExisting") == 0)
			{
				args->whenMatched = WhenMatched_KEEPEXISTING;
			}
			else if (strcmp(value->value.v_utf8.str, "merge") == 0)
			{
				args->whenMatched = WhenMatched_MERGE;
			}
			else if (strcmp(value->value.v_utf8.str, "fail") == 0)
			{
				args->whenMatched = WhenMatched_FAIL;
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"Enumeration value '%s' for field 'whenMatched' is not a valid value.",
									value->value.v_utf8.str),
								errhint(
									"Enumeration value '%s' for field 'whenMatched' is not a valid value.",
									value->value.v_utf8.str)));
			}
		}
		else if (strcmp(key, "whenNotMatched") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(MongoTypeMismatch),
								errmsg(
									"BSON field '$merge.whenNotMatched' is the wrong type '%s', expected type 'string'",
									BsonTypeName(value->value_type)),
								errhint(
									"BSON field '$merge.whenNotMatched' is the wrong type '%s', expected type 'string'",
									BsonTypeName(value->value_type))));
			}

			if (strcmp(value->value.v_utf8.str, "insert") == 0)
			{
				args->whenNotMatched = WhenNotMatched_INSERT;
			}
			else if (strcmp(value->value.v_utf8.str, "discard") == 0)
			{
				args->whenNotMatched = WhenNotMatched_DISCARD;
			}
			else if (strcmp(value->value.v_utf8.str, "fail") == 0)
			{
				args->whenNotMatched = WhenNotMatched_FAIL;
			}
			else
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"Enumeration value '%s' for field '$merge.whenNotMatched' is not a valid value",
									value->value.v_utf8.str),
								errhint(
									"Enumeration value '%s' for field '$merge.whenNotMatched' is not a valid value",
									value->value.v_utf8.str)));
			}
		}
		else
		{
			ereport(ERROR, (errcode(MongoFailedToParse),
							errmsg("BSON field '$merge.%s' is an unknown field", key),
							errhint("BSON field '$merge.%s' is an unknown field", key)));
		}
	}

	if (args->targetCollection.length == 0)
	{
		ereport(ERROR, (errcode(MongoLocation40414),
						errmsg(
							"BSON field '$merge.into' is missing but a required field"),
						errhint(
							"BSON field '$merge.into' is missing but a required field")));
	}

	if (!isOnSpecified)
	{
		args->on.value_type = BSON_TYPE_UTF8;
		args->on.value.v_utf8.len = 3;
		args->on.value.v_utf8.str = "_id";
	}
}


/*
 * Before $merge stage for existing query we need to modify target list for :
 * 1. Generate a object id and add it to the target list, if an object ID is missing during insertion, use the one generated one.
 * 2. Add target collection_id to source tuples so that we can achieve a TRUE equi-join condition. As Citus does not support joins without have equi-join condition on distributed table.
 *
 * After this function new targetList of query will be like :
 * SELECT  collection.document AS document,
 *        '2'::bigint AS target_shard_key_value,  -- (2 is collection_id of target collection)
 *         bson_dollar_merge_generate_object_id(collection.document) AS generated_object_id
 * FROM   mongo_data.documents_1 collection
 * WHERE collection.shard_key_value = '1'::bigint
 *
 * TODO : if source and target collection are same we need to add actual shard_key_value column to the query but need to be careful when there are nested stages
 *        this optimization will help when both collection are sharded so we should do when we support target sharded collection.
 */
static void
RearrangeTargetListForMerge(Query *query, MongoCollection *targetCollection,
							bool isSourceAndTargetAreSame,
							const bson_value_t *onValues)
{
	int resNumber = 0;

	/* Let's create a new target list */

	/* 1. Start by adding the first 'TE' from the existing query, which is a document field.
	 *  target collection is unsharded so add target collection_id to source tuples so that we can achieve
	 *  a TRUE equi-join condition. As Citus does not support joins without have equi-join condition on distributed table.
	 */

	List *newTargetList = NIL;
	TargetEntry *sourceDocTE = (TargetEntry *) linitial(query->targetList);
	resNumber = sourceDocTE->resno;
	newTargetList = lappend(newTargetList, sourceDocTE);

	/* 2. append TE : target_shard_key_value. */
	Expr *exprShardKeyValueCol = (Expr *) makeConst(INT8OID, -1, InvalidOid,
													sizeof(int64),
													Int64GetDatum(
														targetCollection->collectionId),
													false, true);

	TargetEntry *dummySourceShardKeyValueTE = makeTargetEntry(exprShardKeyValueCol,
															  ++resNumber,
															  "target_shard_key_value",
															  false);
	newTargetList = lappend(newTargetList, dummySourceShardKeyValueTE);

	/* 3. append TE : generated_object_id : we can use it while insertion */
	Node *sourceDocVar = (Node *) sourceDocTE->expr;
	List *argsForAddObjectIdFuncExpr = list_make1(sourceDocVar);
	FuncExpr *addObjectIdFuncExpr = makeFuncExpr(BsonDollarMergeGenerateObjectId(),
												 BsonTypeId(),
												 argsForAddObjectIdFuncExpr, InvalidOid,
												 InvalidOid,
												 COERCE_EXPLICIT_CALL);

	TargetEntry *generatedObjectIdTE = makeTargetEntry((Expr *) addObjectIdFuncExpr,
													   ++resNumber,
													   "generated_object_id",
													   false);

	newTargetList = lappend(newTargetList, generatedObjectIdTE);


	if (IsClusterVersionAtleastThis(1, 20, 0))
	{
		/* 4. append bson_dollar_extract_merge_filter function so all on fields so that we can use extracted source in join condition */
		if (onValues->value_type == BSON_TYPE_UTF8)
		{
			newTargetList = lappend(newTargetList,
									MakeExtractFuncExprForMergeTE(
										onValues->value.v_utf8.str,
										onValues->value.v_utf8.
										len,
										(Var *) sourceDocVar,
										++resNumber));
		}
		else if (onValues->value_type == BSON_TYPE_ARRAY)
		{
			bson_iter_t onValuesIter;
			BsonValueInitIterator(onValues, &onValuesIter);

			while (bson_iter_next(&onValuesIter))
			{
				const bson_value_t *innerValue = bson_iter_value(&onValuesIter);
				newTargetList = lappend(newTargetList,
										MakeExtractFuncExprForMergeTE(
											innerValue->value.v_utf8.str,
											innerValue->value.
											v_utf8.len,
											(Var *) sourceDocVar,
											++resNumber));
			}
		}
	}

	/* 4. Move all Remaining entries from the existing target list to the new target list. */
	int targetEntryIndex = 0;
	ListCell *cell;

	foreach(cell, query->targetList)
	{
		TargetEntry *entry = (TargetEntry *) lfirst(cell);
		if (targetEntryIndex == 0)
		{
			targetEntryIndex++;
			continue;
		}

		entry->resno = ++resNumber;
		newTargetList = lappend(newTargetList, entry);
		targetEntryIndex++;
	}

	query->targetList = newTargetList;
}


/* This function creates an target entry for bson_dollar_extract_merge_filter */
static inline TargetEntry *
MakeExtractFuncExprForMergeTE(const char *onField, uint32 length, Var *sourceDocument,
							  const int resNum)
{
	char *resName = psprintf("extracted_%d", resNum);
	Const *onCondition = MakeTextConst(onField, length);
	List *argsForExtractFilterFunc = list_make2(sourceDocument, onCondition);
	FuncExpr *mergeExtractFunction = makeFuncExpr(
		BsonDollarMergeExtractFilterFunctionOid(),
		BsonTypeId(),
		argsForExtractFilterFunc,
		InvalidOid,
		InvalidOid,
		COERCE_EXPLICIT_CALL);
	TargetEntry *extractFuncTE = makeTargetEntry((Expr *) mergeExtractFunction,
												 resNum,
												 resName,
												 false);
	return extractFuncTE;
}


/*
 * Add target collection to the query for $merge aggregation stage.
 */
static inline void
AddTargetCollectionRTEDollarMerge(Query *query, MongoCollection *targetCollection)
{
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	List *colNames = list_make3(makeString("shard_key_value"), makeString("object_id"),
								makeString("document"));
	rte->alias = rte->eref = makeAlias(targetCollection->tableName, colNames);
	rte->rtekind = RTE_RELATION;
	rte->relkind = RELKIND_RELATION;
	rte->self_reference = false;
	rte->lateral = false;
	rte->inh = false;
	rte->inFromCl = true;
	rte->rellockmode = RowExclusiveLock;
	RangeVar *rangeVar = makeRangeVar(ApiDataSchemaName, targetCollection->tableName, -1);
	rte->relid = RangeVarGetRelid(rangeVar, RowExclusiveLock, false);

	#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *permInfo = addRTEPermissionInfo(&query->rteperminfos, rte);
	permInfo->requiredPerms = ACL_SELECT;
	#else
	rte->requiredPerms = ACL_SELECT;
	#endif
	RangeTblEntry *existingrte = list_nth(query->rtable, 0);
	query->rtable = list_make2(rte, existingrte);
	query->resultRelation = 1;
}


/*
 * write join condition to the query Tree for $merge aggregation stage.
 *
 * let's say `on` field is array : ["a", "b", "c"]
 * join condition in sql :
 *
 * ON target.shard_key_value OPERATOR(pg_catalog.=) source.target_shard_key_value
 * AND bson_dollar_merge_join(target.document, source.docuemnt, 'a'::text)
 * AND bson_dollar_merge_join(target.document, source.docuemnt, 'b'::text)
 */
static void
WriteJoinConditionToQueryDollarMerge(Query *query,
									 Var *sourceDocVar,
									 Var *targetDocVar,
									 Var *sourceShardKeyValueVar,
									 Var *targetShardKeyValueVar,
									 Var *targetObjectIdVar,
									 const int sourceExtractedOnFieldsInitIndex,
									 const int sourceCollectionVarNo,
									 MergeArgs mergeArgs)
{
	Expr *opexpr = make_opclause(PostgresInt4EqualOperatorOid(),
								 BOOLOID, false,
								 (Expr *) targetShardKeyValueVar,
								 (Expr *) sourceShardKeyValueVar,
								 InvalidOid,
								 InvalidOid);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 2;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	List *joinFilterList = NIL;
	joinFilterList = lappend(joinFilterList, opexpr);

	int extractFieldResNum = sourceExtractedOnFieldsInitIndex;
	if (mergeArgs.on.value_type == BSON_TYPE_UTF8)
	{
		Expr *singleJoinExpr = CreateSingleJoinExpr(mergeArgs.on.value.v_utf8.str,
													sourceDocVar,
													targetDocVar, targetObjectIdVar,
													extractFieldResNum,
													sourceCollectionVarNo);
		joinFilterList = lappend(joinFilterList, singleJoinExpr);
	}
	else if (mergeArgs.on.value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t onValuesIter;
		BsonValueInitIterator(&mergeArgs.on, &onValuesIter);

		while (bson_iter_next(&onValuesIter))
		{
			const bson_value_t *onValuesElement = bson_iter_value(&onValuesIter);
			const char *onField = onValuesElement->value.v_utf8.str;
			Expr *singleJoinExpr = CreateSingleJoinExpr(onField,
														sourceDocVar,
														targetDocVar, targetObjectIdVar,
														extractFieldResNum,
														sourceCollectionVarNo);
			joinFilterList = lappend(joinFilterList, singleJoinExpr);
			extractFieldResNum++;
		}
	}
	else
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"on field in $merge stage must be either a string or an array of strings, but found %s",
							BsonTypeName(mergeArgs.on.value_type)),
						errhint(
							"on field in $merge stage must be either a string or an array of strings, but found %s",
							BsonTypeName(mergeArgs.on.value_type))));
	}

	query->jointree->quals = (Node *) make_ands_explicit(joinFilterList);
}


/*
 * In the $merge query, users can specify multiple "on" conditions. We handle them in two ways:
 * 1. If the "on" field is "_id", we create an expression: targetDocument.ObjectID = BsonGetValueFunctionOid(sourceDocument, "_id").
 * 2. For any other field, we create an expression: bson_dollar_merge_join(target.document, sourceDocVar.document, "joinfield").
 * The bson_dollar_merge_join function is used to support function for index pushdown, which replaces the function expression with an operator expression.
 */
static inline Expr *
CreateSingleJoinExpr(const char *joinField,
					 Var *sourceDocVar,
					 Var *targetDocVar,
					 Var *targetObjectIdVar,
					 const int extractFieldResNumber,
					 const int sourceCollectionVarNo)
{
	StringView onFieldStringView = CreateStringViewFromString(joinField);
	Const *onCondition = MakeTextConst(onFieldStringView.string,
									   onFieldStringView.length);
	Expr *singleJoinExpr = NULL;
	if (strcmp(joinField, "_id") == 0)
	{
		List *argsforFuncExpr = list_make2(sourceDocVar, onCondition);
		FuncExpr *extractFuncExpr = makeFuncExpr(
			BsonGetValueFunctionOid(), BsonTypeId(), argsforFuncExpr, InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);

		singleJoinExpr = make_opclause(BsonEqualOperatorId(),
									   BOOLOID, false,
									   (Expr *) targetObjectIdVar,
									   (Expr *) extractFuncExpr,
									   InvalidOid, InvalidOid);
	}
	else
	{
		Var *extractedSourceVar = makeVar(sourceCollectionVarNo,
										  extractFieldResNumber, BsonTypeId(), -1, 0, 0);
		List *argsforFuncExpr = list_make3(copyObject(targetDocVar), extractedSourceVar,
										   onCondition);
		singleJoinExpr = (Expr *) makeFuncExpr(
			BsonDollarMergeJoinFunctionOid(), BOOLOID, argsforFuncExpr, InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);
	}

	return singleJoinExpr;
}


/*
 * In the $merge stage, we want to fail if the `on` fields specified in the input do not have a unique index in the target collection.
 * If the target collection does not exist and the `on` field is anything other than `_id`, we also fail.
 *
 * The `on` value can be either a UTF8 string or an array of UTF8 strings.
 * If it is a UTF8 string, we check for a single unique index on that field in the target collection.
 * If it is an array of UTF8 strings, we check for a compound unique index on the specified fields in the target collection.
 * For example, if `on` is "a", we want to ensure that the target collection has a unique index on the field 'a'.
 * If `on` is "[a,b]", we want to ensure that the target collection has a compound unique index on the fields 'a' and 'b'.
 */
static void
VaildateMergeOnFieldValues(const bson_value_t *onValues, uint64 collectionId)
{
	Assert(onValues->value_type == BSON_TYPE_ARRAY ||
		   onValues->value_type == BSON_TYPE_UTF8);
	bool excludeIdIndex = false;
	bool enableNestedDistribution = false;
	List *indexesDetailList = NIL;
	bool foundRequiredIndex = false;
	int numKeysOnField = 1;
	char *keyNameIfSingleKeyJoin = NULL;

	if (onValues->value_type == BSON_TYPE_ARRAY)
	{
		numKeysOnField = BsonDocumentValueCountKeys(onValues);
	}


	if (numKeysOnField == 1)
	{
		if (onValues->value_type == BSON_TYPE_ARRAY)
		{
			bson_iter_t onValuesIter;
			BsonValueInitIterator(onValues, &onValuesIter);
			bson_iter_next(&onValuesIter);
			const bson_value_t *onValue = bson_iter_value(&onValuesIter);
			keyNameIfSingleKeyJoin = onValue->value.v_utf8.str;
		}
		else if (onValues->value_type == BSON_TYPE_UTF8)
		{
			keyNameIfSingleKeyJoin = onValues->value.v_utf8.str;
		}

		/* If the on field contains just the _id field, it's a valid unique index, so we can stop here. */
		if (strcmp(keyNameIfSingleKeyJoin, "_id") == 0)
		{
			return;
		}
	}

	/* By design, collection IDs are always greater than 0. Therefore, if a caller passes a collection ID of 0, it implies that the collection does not exist. */
	if (collectionId != 0)
	{
		indexesDetailList = CollectionIdGetValidIndexes(collectionId, excludeIdIndex,
														enableNestedDistribution);
	}

	ListCell *indexDetailCell = NULL;

	foreach(indexDetailCell, indexesDetailList)
	{
		const IndexDetails *indexDetail = (IndexDetails *) lfirst(indexDetailCell);

		/* The index is required to be unique and should not have any partial filters applied to it. */
		if (indexDetail->indexSpec.indexUnique != BoolIndexOption_True ||
			indexDetail->indexSpec.indexPFEDocument != NULL)
		{
			continue;
		}

		bson_iter_t indexKeyDocumnetIter;
		pgbson *indexKeyDocument = indexDetail->indexSpec.indexKeyDocument;
		PgbsonInitIterator(indexKeyDocument, &indexKeyDocumnetIter);

		if (keyNameIfSingleKeyJoin)
		{
			if (IsSingleUniqueIndexPresent(keyNameIfSingleKeyJoin,
										   &indexKeyDocumnetIter))
			{
				foundRequiredIndex = true;
				break;
			}
		}
		else if (IsCompoundUniqueIndexPresent(onValues, &indexKeyDocumnetIter,
											  numKeysOnField))
		{
			foundRequiredIndex = true;
			break;
		}
	}

	if (!foundRequiredIndex)
	{
		ereport(ERROR, (errcode(MongoLocation51183),
						errmsg(
							"Cannot find index to verify that join fields will be unique"),
						errhint(
							"Cannot find index to verify that join fields will be unique")));
	}
}


/*
 * Checks if a unique index is present for the given field.
 *
 * This function look into elements of indexKeyDocumentIter and checks if a unique index exists for the field specified in the 'onValue'.
 * If indexKeyDocumentIter has more than one document that means it is a compound unique index, so we should ignore that.
 *
 * Parameters:
 * - onValue: Index key string.
 * - indexKeyDocumentIter: An iterator for the index key document.
 *
 * example:
 * - onValue : "apple"
 * - indexKeyDocument : {"apple" : 1}
 * output : true
 *
 * Returns:
 * - true if a unique index is present for the given fields, false otherwise.
 */
static inline bool
IsSingleUniqueIndexPresent(const char *onValue, bson_iter_t *indexKeyDocumnetIter)
{
	pgbsonelement uniqueIndexElement;

	/* if a document contains more than one element, it signifies a compound unique index, such as {"a" : 1, "b" : 1}. we should ignore that */
	if (TryGetSinglePgbsonElementFromBsonIterator(indexKeyDocumnetIter,
												  &uniqueIndexElement))
	{
		if (strcmp(uniqueIndexElement.path, onValue) == 0)
		{
			return true;
		}
	}

	return false;
}


/*
 * Checks if a compound unique index is present for the given fields.
 *
 * This function iterates over index key document and checks if a compound unique index exists for the fields specified in the 'onValues' array.
 *
 * Parameters:
 * - onValues: A bson_value_t of array type.
 * - indexKeyDocumentIter: An iterator for the index key document.
 *
 * example:
 * - onValues : ["a", "b", "c"]
 * - indexKeyDocument : {"b" : 1, "c" : 1, "a" : 1}
 * output : true (as all the element of onvalues are present in indexKeyDocument, so we can say that we found a compound unique index for key a,b,c)
 *
 * Returns:
 * - true if a compound unique index is present for the given fields, false otherwise.
 */
static bool
IsCompoundUniqueIndexPresent(const bson_value_t *onValues,
							 bson_iter_t *indexKeyDocumnetIter,
							 const int totalIndexKeys)
{
	HTAB *onValueHashTable = InitHashTableFromStringArray(onValues, totalIndexKeys);
	int foundCount = 0;

	while (bson_iter_next(indexKeyDocumnetIter))
	{
		StringView currentKey = bson_iter_key_string_view(indexKeyDocumnetIter);
		bool foundInArray = false;
		hash_search(onValueHashTable, &currentKey, HASH_FIND, &foundInArray);
		if (foundInArray)
		{
			foundCount++;
		}
		else
		{
			break;
		}
	}

	hash_destroy(onValueHashTable);

	/* verify that all keys from `indexKeyDocumentIter` are in the hashmap and that their sizes match, ensuring no extra elements in the hashmap." */
	if (foundCount == totalIndexKeys)
	{
		return true;
	}

	return false;
}


/*
 * Initializes a hash table from a string array.
 *
 * This function creates a new hash table and populates it with the strings
 * from the provided array.
 *
 * Parameters:
 * - inputKeyArray: A bson_value_t which must be of BSON_TYPE_ARRAY of strings to be used as keys in the hash table.
 * - arraySize: The size of the inputKeyArray.
 *
 * Returns:
 * - A pointer to the newly created hash table.
 */
static HTAB *
InitHashTableFromStringArray(const bson_value_t *inputKeyArray, int arraySize)
{
	HTAB *hashTable = CreateStringViewHashSet();

	bson_iter_t inputArrayIter;
	BsonValueInitIterator(inputKeyArray, &inputArrayIter);

	while (bson_iter_next(&inputArrayIter))
	{
		const bson_value_t *inputArrayElement = bson_iter_value(&inputArrayIter);
		StringView value = CreateStringViewFromStringWithLength(
			inputArrayElement->value.v_utf8.str,
			inputArrayElement->value.
			v_utf8.len);
		hash_search(hashTable, &value, HASH_ENTER, NULL);
	}

	return hashTable;
}


/*
 * ValidatePreviousStagesOfDollarMerge traverse query tree to fail early if $merge is used with $graphLookup
 */
static inline bool
ValidatePreviousStagesOfDollarMerge(Query *query)
{
	/* An example of this could be when the target collection for the $lookup operation is missing, and the empty_data_table function is invoked. */
	if (contain_mutable_functions((Node *) query))
	{
		ereport(ERROR, (errcode(MongoCommandNotSupported),
						errmsg(
							"The `$merge` stage is not supported with this command. If your query references any non-existent collections, please create them and try again."),
						errhint(
							"MUTABLE functions are not yet in MERGE command by citus")));
	}

	return query_tree_walker(query, MergeQueryCTEWalker, NULL, 0);
}


/*
 * MergeQueryCTEWalker descends into the MERGE query to check for any subqueries
 */
static bool
MergeQueryCTEWalker(Node *node, void *context)
{
	if (node == NULL)
	{
		return false;
	}

	if (IsA(node, Query))
	{
		Query *query = (Query *) node;

		if (query->hasRecursive)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg(
								"$graphLookup is not supported with $merge stage yet."),
							errhint(
								"$graphLookup is not supported with $merge stage yet.")));
		}

		query_tree_walker(query, MergeQueryCTEWalker, NULL, 0);

		/* we're done, no need to recurse anymore for this query */
		return false;
	}

	return expression_tree_walker(node, MergeQueryCTEWalker, context);
}


/* let's validate final pgbson before writing to collection */
static inline void
ValidateFinalPgbsonBeforeWriting(const pgbson *finalBson)
{
	/* let's validate final document before insert */
	PgbsonValidateInputBson(finalBson, BSON_VALIDATE_NONE);
	if (finalBson != NULL)
	{
		uint32_t size = PgbsonGetBsonSize(finalBson);
		if (size > BSON_MAX_ALLOWED_SIZE)
		{
			ereport(ERROR, (errcode(MongoBsonObjectTooLarge),
							errmsg("Size %u is larger than MaxDocumentSize %u",
								   size, BSON_MAX_ALLOWED_SIZE)));
		}
	}
}


/*
 * This function Validate the ObjectId fields and write it in the writer.
 *
 * During validation, it addresses the following MongoDB behavior:
 * 1. If the ObjectId of the source and target documents differ, an error is thrown because the target ObjectId cannot be replaced with the source ObjectId, as the ObjectId field is immutable.
 * 2. If the ObjectId of the source and target documents are the same, the ObjectId field is written to the writer.
 * 3. If the source ObjectId is missing we write the target ObjectId to the writer.
 *
 * Parameters:
 *   - writer: The pgbson_writer to which the "_id" field will be added.
 *   - sourceDocument: The source pgbson document.
 *   - targetDocument: The target pgbson document.
 */
static void
ValidateAndAddObjectIdToWriter(pgbson_writer *writer,
							   pgbson *sourceDocument,
							   pgbson *targetDocument)
{
	/* Here we expect _id to be the first field of the target document because while inserting we make sure to move _id to the first field and write to the table */
	pgbsonelement objectIdFromTargetDocument;

	if (!TryGetSinglePgbsonElementFromPgbson(targetDocument,
											 &objectIdFromTargetDocument) &&
		strcmp(objectIdFromTargetDocument.path, "_id") != 0)
	{
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg(
							"Something went wrong, Expecting object ID to be the first field in the target document"),
						errhint(
							"Something went wrong, Expecting object ID to be the first field in the target document of type %s",
							BsonTypeName(
								objectIdFromTargetDocument.bsonValue.value_type))));
	}

	bson_iter_t sourceIter;
	if (PgbsonInitIteratorAtPath(sourceDocument, "_id", &sourceIter))
	{
		const bson_value_t *value = bson_iter_value(&sourceIter);

		/* compare source _id value with target's _id value, As object id is Immutable both field should match */
		bool ignoreIsCmpValid = true;
		if (CompareBsonValueAndType(value, &objectIdFromTargetDocument.bsonValue,
									&ignoreIsCmpValid) != 0)
		{
			/* We validate the object ID in failure scenarios to ensure that if the ID is incorrect, we return the appropriate error code initially. */
			ValidateIdField(value);
			ereport(ERROR, (errcode(MongoImmutableField),
							errmsg(
								"$merge failed to update the matching document, did you attempt to modify the _id or the shard key?")));
		}
	}

	/* Everything looks good, we can write the `_id` field to the writer */
	PgbsonWriterInit(writer);
	PgbsonWriterAppendValue(writer, "_id", 3, &objectIdFromTargetDocument.bsonValue);
}


/*
 * write join condition to the query Tree for $merge aggregation stage.
 *
 * let's say `on` field is array : ["a", "b", "c"]
 * join condition in sql :
 *
 * ON target.shard_key_value OPERATOR(pg_catalog.=) source.target_shard_key_value
 * AND bson_dollar_merge_join(target.document, source.docuemnt, 'a'::text)
 * AND bson_dollar_merge_join(target.document, source.docuemnt, 'b'::text)
 *
 * TODO : Remove this after release 1.20
 */
static void
WriteJoinConditionToQueryDollarMergeLegacy(Query *query, Var *sourceDocVar,
										   Var *targetDocVar,
										   Var *sourceShardKeyValueVar,
										   Var *targetShardKeyValueVar, MergeArgs
										   mergeArgs)
{
	Expr *opexpr = make_opclause(PostgresInt4EqualOperatorOid(),
								 BOOLOID, false,
								 (Expr *) targetShardKeyValueVar,
								 (Expr *) sourceShardKeyValueVar,
								 InvalidOid,
								 InvalidOid);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 2;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	List *joinFilterList = NIL;
	joinFilterList = lappend(joinFilterList, opexpr);

	if (mergeArgs.on.value_type == BSON_TYPE_UTF8)
	{
		const char *onField = mergeArgs.on.value.v_utf8.str;
		StringView onFieldStringView = CreateStringViewFromString(onField);
		Const *onCondition = MakeTextConst(onFieldStringView.string,
										   onFieldStringView.length);

		List *argsforFuncExpr = list_make3(targetDocVar, sourceDocVar, onCondition);
		FuncExpr *onConditionExpr = makeFuncExpr(
			BsonDollarMergeJoinFunctionOid(), BOOLOID, argsforFuncExpr, InvalidOid,
			InvalidOid, COERCE_EXPLICIT_CALL);

		joinFilterList = lappend(joinFilterList, onConditionExpr);
	}
	else if (mergeArgs.on.value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t onValuesIter;
		BsonValueInitIterator(&mergeArgs.on, &onValuesIter);

		while (bson_iter_next(&onValuesIter))
		{
			const bson_value_t *onValuesElement = bson_iter_value(&onValuesIter);

			const char *onField = onValuesElement->value.v_utf8.str;
			StringView onFieldStringView = CreateStringViewFromString(onField);
			Const *onCondition = MakeTextConst(onFieldStringView.string,
											   onFieldStringView.length);

			List *argsforFuncExpr = list_make3(targetDocVar, sourceDocVar, onCondition);
			FuncExpr *onConditionExpr = makeFuncExpr(
				BsonDollarMergeJoinFunctionOid(), BOOLOID, argsforFuncExpr, InvalidOid,
				InvalidOid, COERCE_EXPLICIT_CALL);

			joinFilterList = lappend(joinFilterList, onConditionExpr);
		}
	}
	else
	{
		ereport(ERROR, (errcode(MongoFailedToParse),
						errmsg(
							"on field in $merge stage must be either a string or an array of strings, but found %s",
							BsonTypeName(mergeArgs.on.value_type)),
						errhint(
							"on field in $merge stage must be either a string or an array of strings, but found %s",
							BsonTypeName(mergeArgs.on.value_type))));
	}

	query->jointree->quals = (Node *) make_ands_explicit(joinFilterList);
}
