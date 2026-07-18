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
#include <executor/spi.h>

#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "query/bson_compare.h"
#include "planner/documentdb_planner.h"
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
#include "utils/query_utils.h"
#include "utils/fmgr_utils.h"
#include "schema_validation/schema_validation.h"

#include "aggregation/bson_aggregation_pipeline_private.h"

/*
 * $merge stage input field `WhenMatched` options
 */
typedef enum WhenMatchedAction
{
	WhenMatched_MERGE = 0,
	WhenMatched_REPLACE = 1,
	WhenMatched_KEEPEXISTING = 2,
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

	/* name to the specified input target collection */
	StringView targetCollection;

	/* input `on` field can be an array or string */
	bson_value_t on;

	/* input `whenMatched` field */
	WhenMatchedAction whenMatched;

	/* input `whenNotMatched` field */
	WhenNotMatchedAction whenNotMatched;
} MergeArgs;


/*
 * Struct having parsed view of the arguments to $out stage.
 */
typedef struct OutArgs
{
	/* name of input target Databse */
	StringView targetDB;

	/* name to the specified input target collection */
	StringView targetCollection;
} OutArgs;


/* GUC to enable $out aggregation stage */
extern bool EnableCollation;

/* GUC to enable schema validation */
extern bool EnableSchemaValidation;

static void ParseMergeStage(const bson_value_t *existingValue, const
							char *currentNameSpace, MergeArgs *args);
static void ParseOutStage(const bson_value_t *existingValue, const char *currentNameSpace,
						  OutArgs *args);
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
										   Var *targetDocVar,
										   Const *schemaValidatorInfoConst,
										   Const *validationLevelConst);
static MergeAction * MakeActionWhenNotMatched(WhenNotMatchedAction whenNotMatched,
											  Var *sourceDocVar,
											  Var *generatedObjectIdVar,
											  Var *sourceShardKeyVar,
											  MongoCollection *targetCollection,
											  Const *SchemaValidatorInfoConst);
static bool IsCompoundUniqueIndexPresent(const bson_value_t *onValues,
										 bson_iter_t *indexKeyDocumentIter,
										 const int numElementsInMap);
static void ValidateAndAddObjectIdToWriter(pgbson_writer *writer,
										   pgbson *sourceDocument,
										   pgbson *targetDocument);
static inline bool IsSingleUniqueIndexPresent(const char *onValue,
											  bson_iter_t *indexKeyDocumentIter);
static inline void AddTargetCollectionRTEDollarMerge(Query *query,
													 MongoCollection *targetCollection);
static HTAB * InitHashTableFromStringArray(const bson_value_t *onValues, int
										   onValuesArraySize);
static inline void ValidatePreOutputStages(Query *query, char *stageName);
static bool MergeQueryCTEWalker(Node *node, void *context);
static inline void ValidateFinalPgbsonBeforeWriting(const pgbson *finalBson, const
													pgbson *targetDocument,
													ExprEvalState *
													stateForSchemaValidation,
													ValidationLevels
													validationLevel);
static inline Expr * CreateSingleJoinExpr(const char *joinField,
										  Var *sourceDocVar,
										  Var *targetDocVar,
										  Var *targetObjectIdVar,
										  const int extractFieldResNumber,
										  const int sourceCollectionVarNo);
static inline TargetEntry * MakeExtractFuncExprForMergeTE(const char *onField, uint32
														  length, Var *sourceDocument,
														  const int resNum);
static void TruncateDataTable(int collectionId);
static inline bool CheckSchemaValidationEnabledForDollarMergeOut(void);
static inline void ValidateTargetNameSpaceForOutputStage(const StringView *targetDB,
														 const StringView *
														 targetCollection,
														 bool isMergeStage);

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

		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51132),
						errmsg(
							"Write operation for $merge failed: the 'on' field must be provided and cannot be null, undefined, or an array"),
						errdetail_log(
							"Write operation for $merge failed: the 'on' field must be provided and cannot be null, undefined, or an array")));
	}

	pgbsonelement filterElement;
	filterElement.path = joinField;
	filterElement.pathLength = strlen(joinField);
	filterElement.bsonValue = *bson_iter_value(&sourceIter);

	if (filterElement.bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51185),
						errmsg(
							"Write operation for $merge failed: the 'on' field must be provided and cannot be null, undefined, or an array"),
						errdetail_log(
							"Write operation for $merge failed: the 'on' field must be provided and cannot be null, undefined, or an array")));
	}
	else if (filterElement.bsonValue.value_type == BSON_TYPE_NULL ||
			 filterElement.bsonValue.value_type == BSON_TYPE_UNDEFINED)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51132),
						errmsg(
							"Write operation for $merge failed: the 'on' field must be provided and cannot be null, undefined, or an array"),
						errdetail_log(
							"Write operation for $merge failed: the 'on' field must be provided and cannot be null, undefined, or an array")));
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

	/* If evalStateBytea is not NULL, we need to parse it to get the schema validation state. */
	ExprEvalState *stateForSchemaValidation = NULL;
	if (CheckSchemaValidationEnabledForDollarMergeOut() && PG_NARGS() > 2)
	{
		pgbson *schemaValidatorInfo = PG_GETARG_MAYBE_NULL_PGBSON(2);

		if (!IsPgbsonEmptyDocument(schemaValidatorInfo))
		{
			int argPositions = 2;
			SetCachedFunctionState(
				stateForSchemaValidation,
				ExprEvalState,
				argPositions,
				AssignSchemaValidationState,
				schemaValidatorInfo,
				CurrentMemoryContext);
			if (stateForSchemaValidation == NULL)
			{
				stateForSchemaValidation = palloc0(sizeof(ExprEvalState));
				AssignSchemaValidationState(stateForSchemaValidation, schemaValidatorInfo,
											CurrentMemoryContext);
			}
		}
	}

	/* Add and validate _id */
	pgbson *outputBson = RewriteDocumentWithCustomObjectId(sourceDocument,
														   generatedObjectID);
	ValidateFinalPgbsonBeforeWriting(outputBson, NULL, stateForSchemaValidation,
									 ValidationLevel_Strict);

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

	/* If evalStateBytea is not NULL, we need to parse it to get the schema validation state and set the validation level. */
	ExprEvalState *stateForSchemaValidation = NULL;
	ValidationLevels validationLevel = ValidationLevel_Invalid;

	/* special case - Schema validation is not performed if the source document is the same as the target document. */
	bool performSchemaValidation = false;
	bool needComparison = true;

	if (CheckSchemaValidationEnabledForDollarMergeOut() && PG_NARGS() > 3)
	{
		pgbson *schemaValidatorInfo = PG_GETARG_MAYBE_NULL_PGBSON(3);

		if (!IsPgbsonEmptyDocument(schemaValidatorInfo))
		{
			performSchemaValidation = true;
			validationLevel = PG_ARGISNULL(4) ? ValidationLevel_Invalid : PG_GETARG_INT32(
				4);
		}
	}

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
			bson_iter_t iter;
			PgbsonInitIterator(targetDocument, &iter);

			/* _id is already written to writer as first field of writer. so ignore for target document. */
			if (!bson_iter_next(&iter) || strcmp(bson_iter_key(&iter), "_id") != 0)
			{
				/* In target document we expect _id to be first field. */
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg(
									"$merge write error: target document missing _id field as first field"),
								errdetail_log(
									"$merge write error: target document missing _id field as first field")));
			}

			HTAB *hashTable = CreatePgbsonElementOrderedHashSet();
			PgbsonElementHashEntryOrdered *head = NULL;
			PgbsonElementHashEntryOrdered *tail = NULL;

			/* step 1 : Add target document to the hashmap with values and maintain order using PgbsonElementHashEntryOrdered linked list */
			while (bson_iter_next(&iter))
			{
				pgbsonelement element = {
					.path = bson_iter_key(&iter),
					.pathLength = bson_iter_key_len(&iter),
					.bsonValue = *bson_iter_value(&iter)
				};

				PgbsonElementHashEntryOrdered hashEntry = {
					.element = element,
					.next = NULL,
				};

				bool found = false;
				PgbsonElementHashEntryOrdered *currNode = hash_search(hashTable,
																	  &hashEntry,
																	  HASH_ENTER, &found);

				if (head == NULL)
				{
					head = currNode;
					tail = currNode;
				}
				else
				{
					tail->next = currNode;
					tail = currNode;
				}
			}

			/* step 2 : let's add source document to hashmap with values and update the tail of the linked list if a new element is inserted */
			PgbsonInitIterator(sourceDocument, &iter);

			while (bson_iter_next(&iter))
			{
				/* _id is already written to writer as the first field, so ignore it here */
				if (strcmp(bson_iter_key(&iter), "_id") == 0)
				{
					continue;
				}

				pgbsonelement element = {
					.path = bson_iter_key(&iter),
					.pathLength = bson_iter_key_len(&iter),
					.bsonValue = *bson_iter_value(&iter)
				};

				PgbsonElementHashEntryOrdered hashEntry = {
					.element = element,
					.next = NULL,
				};

				bool found = false;
				PgbsonElementHashEntryOrdered *currNode = hash_search(hashTable,
																	  &hashEntry,
																	  HASH_ENTER,
																	  &found);

				if (found)
				{
					/* Replace the existing value with the value from the source document */
					currNode->element.bsonValue = element.bsonValue;
				}
				else if (head == NULL)
				{
					/* If the target document contains only the _id field, we reach here */
					head = currNode;
					tail = currNode;
					needComparison = false;
				}
				else
				{
					tail->next = currNode;
					tail = currNode;
					needComparison = false;
				}
			}


			/* step 3: Iterate through the linked list to fetch elements in order and write them to the final BSON */
			while (head != NULL)
			{
				PgbsonElementHashEntryOrdered *temp = head;
				PgbsonWriterAppendValue(&writer, temp->element.path,
										temp->element.pathLength,
										&temp->element.bsonValue);
				head = head->next;
			}

			hash_destroy(hashTable);
			finalDocument = PgbsonWriterGetPgbson(&writer);

			break;
		}

		case WhenMatched_KEEPEXISTING:
		{
			/* we are not suppose to reach here if action is `WhenMatched_KEEPEXISTING` we should set `DO NOTHING` Action of PG */
			ereport(ERROR, errcode(ERRCODE_DOCUMENTDB_INTERNALERROR), (errmsg(
																		   "whenMathed KeepEXISTING should not reach here"),
																	   errdetail_log(
																		   "whenMathed KeepEXISTING should not reach here")));
		}

		case WhenMatched_FAIL:
		{
			/*
			 * Compatibility Notice: The text in this error string is copied verbatim from MongoDB output to maintain
			 * compatibility with existing tools and scripts that rely on specific error message formats. Modifying
			 * this text may cause unexpected behavior in dependent systems.
			 *
			 * JsTest to resolve: mode_fail_insert.js
			 */
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DUPLICATEKEY),
							errmsg(
								"$merge with whenMatched: fail found an existing document with the same values for the 'on' fields"),
							errdetail_log(
								"$merge with whenMatched: fail found an existing document with the same values for the 'on' fields")));
		}

		case WhenMatched_PIPELINE:
		case WhenMatched_LET:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"merge, pipeline and Let option not supported yet in whenMatched field of $merge aggreagtion stage"),
							errdetail_log(
								"merge, pipeline and Let option not supported yet in whenMatched field of $merge aggreagtion stage")));
		}

		default:
		{
			ereport(ERROR, errcode(ERRCODE_DOCUMENTDB_INTERNALERROR), (errmsg(
																		   "Unrecognized WhenMatched value"),
																	   errdetail_log(
																		   "Unrecognized WhenMatched value")));
		}
	}

	/* needComparison = true means we need to compare source and target document to determine whether to skip schema validation or not. */
	if (CheckSchemaValidationEnabledForDollarMergeOut())
	{
		if (needComparison && performSchemaValidation)
		{
			performSchemaValidation = PgbsonEquals(sourceDocument, targetDocument) ?
									  false : true;
		}

		if (performSchemaValidation)
		{
			pgbson *schemaValidatorInfo = PG_GETARG_PGBSON(3);
			int argPositions = 3;
			SetCachedFunctionState(
				stateForSchemaValidation,
				ExprEvalState,
				argPositions,
				AssignSchemaValidationState,
				schemaValidatorInfo,
				CurrentMemoryContext);
			if (stateForSchemaValidation == NULL)
			{
				stateForSchemaValidation = palloc0(sizeof(ExprEvalState));
				AssignSchemaValidationState(stateForSchemaValidation, schemaValidatorInfo,
											CurrentMemoryContext);
			}
		}
	}

	/* let's validate final document before writing */
	ValidateFinalPgbsonBeforeWriting(finalDocument, targetDocument,
									 stateForSchemaValidation, validationLevel);

	PG_RETURN_POINTER(finalDocument);
}


/*
 * In the `$merge` stage, to handle `fail` action of `WhenNotMatched` case.
 * This function accepts dummy arguments and has return type to prevent PostgreSQL from treating it as a constant function and evaluating it prematurely.
 */
Datum
bson_dollar_merge_fail_when_not_matched(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_MERGESTAGENOMATCHINGDOCUMENT),
					errmsg(
						"$merge failed to locate a corresponding document in the target collection for one or more documents from the source collection"),
					errdetail_log(
						"$merge failed to locate a corresponding document in the target collection for one or more documents from the source collection")));

	PG_RETURN_NULL();
}


/*
 * Mutates the query for the $merge stage
 *
 * Example : { $merge: { into: <>, on: <>, whenMatched: <>, whenNotMatched: <> } }
 * target collection with schema validation `{ "a" : { "$type" : "int" } }` and validationLevel is `strict`
 * sql query :
 *
 * MERGE INTO ONLY ApiDataSchemaName.documents_2 documents_2
 * USING (
 *          SELECT collection.document AS document,
 *                 '2'::bigint AS target_shard_key_value,  -- (2 is collection_id of target collection)
 *                  bson_dollar_merge_generate_object_id(collection.document) AS generated_object_id
 *			FROM ApiDataSchemaName.documents_1 collection
 *			WHERE collection.shard_key_value = '1'::bigint
 *		 ) agg_stage_0
 * ON documents_2.shard_key_value OPERATOR(pg_catalog.=) agg_stage_0.target_shard_key_value
 * AND bson_dollar_merge_join(documents_2.document, agg_stage_0.document, '_id'::text)
 * WHEN MATCHED
 * THEN
 *      UPDATE SET document = bson_dollar_merge_handle_when_matched(agg_stage_0.document, documents_2.document, 1, '{ "a" : { "$type" : "int" } }'::bson, 1)
 * WHEN NOT MATCHED
 * THEN
 *      INSERT (shard_key_value, object_id, document, creation_time)
 *      VALUES (agg_stage_0.target_shard_key_value,
 * COALESCE(bson_get_value(agg_stage_0.document, '_id'::text), agg_stage_0.generated_object_id), bson_dollar_merge_add_object_id(agg_stage_0.document, agg_stage_0.generated_object_id, '{ "a" : { "$type" : "int" } }'::bson), '2024-12-16 10:00:57.196789+00'::timestamp with time zone);
 *
 */
Query *
HandleMerge(const bson_value_t *existingValue, Query *query,
			AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_MERGE);

	if (IsCollationApplicable(context->collationString))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED), errmsg(
							"collation is not supported with $merge yet")));
	}

	bool isTopLevel = true;
	if (IsInTransactionBlock(isTopLevel))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_OPERATIONNOTSUPPORTEDINTRANSACTION),
						errmsg(
							"$merge is not permitted within an active transaction")));
	}

	/* if source table does not exist do not modify query */
	if (context->mongoCollection == NULL)
	{
		return query;
	}

	ValidatePreOutputStages(query, "$merge");

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
		bool isMergeStage = true;
		ValidateTargetNameSpaceForOutputStage(&mergeArgs.targetDB,
											  &mergeArgs.targetCollection, isMergeStage);
		int ignoreCollectionID = 0;
		VaildateMergeOnFieldValues(&mergeArgs.on, ignoreCollectionID);
		targetCollection = CreateCollectionForInsert(databaseNameDatum,
													 collectionNameDatum);
	}
	else
	{
		if (targetCollection->viewDefinition != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTEDONVIEW),
							errmsg(
								"The namespace %s.%s refers to a view object rather than a collection",
								targetCollection->name.databaseName,
								targetCollection->name.collectionName),
							errdetail_log(
								"The namespace %s.%s refers to a view object rather than a collection",
								targetCollection->name.databaseName,
								targetCollection->name.collectionName)));
		}
		else if (targetCollection->shardKey != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"$merge for sharded output collection not supported yet"),
							errdetail_log(
								"$merge for sharded output collection not supported yet")));
		}

		VaildateMergeOnFieldValues(&mergeArgs.on, targetCollection->collectionId);
	}

	bool isSourceAndTargetAreSame = (targetCollection->collectionId ==
									 context->mongoCollection->collectionId);

	/* constant for target collection */
	const int targetCollectionVarNo = 1;     /* In merge query target table is 1st table */
	const int targetShardKeyValueAttrNo = 1; /* From Target table we are just selecting 3 columns first one is shard_key_value */
	const int targetObjectIdAttrNo = 2;      /* From Target table we are just selecting 3 columns first one is shard_key_value */
	const int targetDocAttrNo = 3;           /* From Target table we are just selecting 3 columns third one is document */

	/* Constant value assigned for source collection */
	const int sourceCollectionVarNo = 2;            /* In merge query source table is 2nd table */
	const int sourceDocAttrNo = 1;                  /* In source table first projector is document */
	const int sourceShardKeyValueAttrNo = 2;        /* we will append shard_key_value in source query at 2nd position after document column */
	const int generatedObjectIdAttrNo = 3;          /* we will append generated object_id in source query at 3rd position after shard_key_value column */
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
	Const *schemaValidatorInfoConst = MakeBsonConst(PgbsonInitEmpty());
	Const *validationLevelConst = makeConst(INT4OID, -1, InvalidOid, 4,
											Int32GetDatum(ValidationLevel_Invalid),
											false, true);
	bool bypassDocumentValidation = false;
	if (CheckSchemaValidationEnabled(targetCollection, bypassDocumentValidation))
	{
		schemaValidatorInfoConst = MakeBsonConst(
			targetCollection->schemaValidator.validator);
		validationLevelConst = makeConst(INT4OID, -1, InvalidOid, 4,
										 Int32GetDatum(
											 targetCollection->schemaValidator.
											 validationLevel),
										 false, true);
	}

	query->mergeActionList = list_make2(MakeActionWhenMatched(mergeArgs.whenMatched,
															  sourceDocVar, targetDocVar,
															  schemaValidatorInfoConst,
															  validationLevelConst),
										MakeActionWhenNotMatched(mergeArgs.whenNotMatched,
																 sourceDocVar,
																 generatedObjectIdVar,
																 sourceShardKeyValueVar,
																 targetCollection,
																 schemaValidatorInfoConst));
	WriteJoinConditionToQueryDollarMerge(query, sourceDocVar, targetDocVar,
										 sourceShardKeyValueVar,
										 targetShardKeyValueVar,
										 targetObjectIdVar,
										 sourceExtractedOnFieldsInitIndex,
										 sourceCollectionVarNo,
										 mergeArgs);
	return query;
}


/*
 * create MergeAction for `whenMatched` case.
 * This function is responsible for constructing the following segment of the merge query :
 * WHEN MATCHED THEN
 * UPDATE SET document = bson_dollar_merge_handle_when_matched(agg_stage_4.document, documents_1.document, 0, '{ "a" : { "$type" : "int" } }'::bson, 1)
 */
static MergeAction *
MakeActionWhenMatched(WhenMatchedAction whenMatched, Var *sourceDocVar, Var *targetDocVar,
					  Const *schemaValidatorInfoConst, Const *validationLevelConst)
{
	MergeAction *action = makeNode(MergeAction);
#if PG_VERSION_NUM >= 170000
	action->matchKind = MERGE_WHEN_MATCHED;
#else
	action->matched = true;
#endif

	if (whenMatched == WhenMatched_KEEPEXISTING)
	{
		action->commandType = CMD_NOTHING;
		return action;
	}

	action->commandType = CMD_UPDATE;
	Const *inputActionForWhenMathced = makeConst(INT4OID, -1, InvalidOid, sizeof(int32),
												 Int32GetDatum(whenMatched),
												 false, true);
	List *args = NIL;
	args = list_make5(sourceDocVar, targetDocVar, inputActionForWhenMathced,
					  schemaValidatorInfoConst, validationLevelConst);

	FuncExpr *resultExpr = makeFuncExpr(
		BsonDollarMergeHandleWhenMatchedFunctionOid(), BsonTypeId(), args, InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	action->targetList = list_make1(
		makeTargetEntry((Expr *) resultExpr,
						DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER, "document", false)
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
 *        source.document), bson_dollar_merge_add_object_id(source.document, generated_object_id, schema_validator_info),
 *        <current-time>)
 */
static MergeAction *
MakeActionWhenNotMatched(WhenNotMatchedAction whenNotMatched, Var *sourceDocVar,
						 Var *generatedObjectIdVar,
						 Var *sourceShardKeyVar, MongoCollection *targetCollection,
						 Const *schemaValidatorInfoConst)
{
	MergeAction *action = makeNode(MergeAction);
#if PG_VERSION_NUM >= 170000
	action->matchKind = MERGE_WHEN_NOT_MATCHED_BY_TARGET;
#else
	action->matched = false;
#endif

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
	List *argsForAddObjecIdFunc = NIL;
	argsForAddObjecIdFunc = list_make3(sourceDocVar, generatedObjectIdVar,
									   schemaValidatorInfoConst);

	FuncExpr *addObjecIdFuncExpr = makeFuncExpr(
		BsonDollarMergeAddObjectIdFunctionOid(), BsonTypeId(), argsForAddObjecIdFunc,
		InvalidOid,
		InvalidOid, COERCE_EXPLICIT_CALL);

	/* for insert operation */
	action->targetList = list_make3(
		makeTargetEntry((Expr *) sourceShardKeyVar,
						DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER,
						"target_shard_key_value", false),
		makeTargetEntry((Expr *) coalesce,
						DOCUMENT_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER, "object_id",
						false),
		makeTargetEntry((Expr *) addObjecIdFuncExpr,
						DOCUMENT_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER, "document",
						false));

	if (targetCollection->mongoDataCreationTimeVarAttrNumber != -1)
	{
		action->targetList = lappend(action->targetList,
									 makeTargetEntry((Expr *) nowValue,
													 targetCollection->
													 mongoDataCreationTimeVarAttrNumber,
													 "creation_time",
													 false));
	}

	return action;
}


/*
 * Parses & validates the input $merge spec.
 *
 * { $merge: {
 *     into: <>,
 *     on: <>,
 *     let: <>,
 *     whenMatched: <>,
 *    whenNotMatched: <>
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
						errmsg(
							"The $merge operator needs a string or object input, but received %s instead",
							BsonTypeName(
								existingValue->value_type)),
						errdetail_log(
							"The $merge operator needs a string or object input, but received %s instead",
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
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
										errmsg(
											"BSON field 'into.%s' has an incorrect type '%s'; the expected data type is 'string'",
											innerKey, BsonTypeName(value->value_type)),
										errdetail_log(
											"BSON field 'into.%s' has an incorrect type '%s'; the expected data type is 'string'",
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
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
										errmsg(
											"BSON field 'into.%s' is not recognized as a valid field",
											innerKey),
										errdetail_log(
											"BSON field 'into.%s' is not recognized as a valid field",
											innerKey)));
					}
				}

				if (args->targetCollection.length == 0)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51178),
									errmsg(
										"The 'into' field of $merge must define a collection name that is neither empty, null, nor undefined."),
									errdetail_log(
										"The 'into' field of $merge must define a collection name that is neither empty, null, nor undefined.")));
				}
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51178),
								errmsg(
									"$merge 'into' field must contain either a string value or an object, but was given %s instead",
									BsonTypeName(value->value_type)),
								errdetail_log(
									"$merge 'into' field must contain either a string value or an object, but was given %s instead",
									BsonTypeName(value->value_type))));
			}

			StringView nameSpaceView = CreateStringViewFromString(currentNameSpace);
			StringView currentDBName = StringViewFindPrefix(&nameSpaceView, '.');

			/* if target database name not mentioned in input let's use source database */
			if (args->targetDB.length == 0)
			{
				args->targetDB = currentDBName;
			}
		}
		else if (strcmp(key, "on") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8 && value->value_type !=
				BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51186),
								errmsg(
									"The $merge 'on' field must be provided as either a single string or an array containing multiple strings, but received %s instead.",
									BsonTypeName(value->value_type)),
								errdetail_log(
									"The $merge 'on' field must be provided as either a single string or an array containing multiple strings, but received %s instead.",
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
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51134),
										errmsg(
											"Array elements for $merge 'on' must be strings, but a %s type was detected",
											BsonTypeName(onValuesElement->value_type)),
										errdetail_log(
											"Array elements for $merge 'on' must be strings, but a %s type was detected",
											BsonTypeName(onValuesElement->value_type))));
					}
				}

				if (!atLeastOneElement)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51187),
									errmsg(
										"When you explicitly set the operators merge to 'on', you are required to include at least one field."),
									errdetail_log(
										"When you explicitly set the operators merge to 'on', you are required to include at least one field.")));
				}
			}

			args->on = *value;
			isOnSpecified = true;
		}
		else if (strcmp(key, "let") == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("The let option is currently unsupported"),
							errdetail_log(
								"The let option is currently unsupported")));
		}
		else if (strcmp(key, "whenMatched") == 0)
		{
			if (value->value_type == BSON_TYPE_ARRAY)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
								errmsg(
									"$merge 'whenMatched' with 'pipeline' not supported yet"),
								errdetail_log(
									"$merge 'whenMatched' with 'pipeline' not supported yet")));
			}
			else if (value->value_type != BSON_TYPE_UTF8)
			{
				/* TODO : Modify error text when we support pipeline. Replace `must be string` with `must be either a string or array` */
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51191),
								errmsg(
									"The 'whenMatched' field in $merge needs to be a string value, but a %s type was provided.",
									BsonTypeName(
										value->value_type)),
								errdetail_log(
									"The 'whenMatched' field in $merge needs to be a string value, but a %s type was provided.",
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"The enumeration value '%s' specified for the 'whenMatched' field is invalid.",
									value->value.v_utf8.str),
								errdetail_log(
									"The enumeration value '%s' specified for the 'whenMatched' field is invalid.",
									value->value.v_utf8.str)));
			}
		}
		else if (strcmp(key, "whenNotMatched") == 0)
		{
			if (value->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_TYPEMISMATCH),
								errmsg(
									"BSON field '$merge.whenNotMatched' has an incorrect type '%s', but it should be of type 'string'",
									BsonTypeName(value->value_type)),
								errdetail_log(
									"BSON field '$merge.whenNotMatched' has an incorrect type '%s', but it should be of type 'string'",
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"The enumeration value '%s' provided for the field operator '$merge.whenNotMatched' is invalid.",
									value->value.v_utf8.str),
								errdetail_log(
									"The enumeration value '%s' provided for the field operator '$merge.whenNotMatched' is invalid.",
									value->value.v_utf8.str)));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg(
								"The BSON field '$merge.%s' is not recognized as a valid field.",
								key),
							errdetail_log(
								"The BSON field '$merge.%s' is not recognized as a valid field.",
								key)));
		}
	}

	if (args->targetCollection.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION40414),
						errmsg(
							"The required BSON field '$merge.into' is missing."),
						errdetail_log(
							"The required BSON field '$merge.into' is missing.")));
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
 * FROM   ApiDataSchemaName.documents_1 collection
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

	/* 4. append bson_dollar_extract_merge_filter function so all on fields so that we can use extracted source in join condition.
	 *    For the $out stage, we will have 'on' values, so this step will be skipped for $out.
	 */

	if (onValues)
	{
		if (onValues->value_type == BSON_TYPE_UTF8)
		{
			newTargetList = lappend(newTargetList,
									MakeExtractFuncExprForMergeTE(
										onValues->value.v_utf8.str,
										onValues->value.v_utf8.len,
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
											innerValue->value.v_utf8.len,
											(Var *) sourceDocVar,
											++resNumber));
			}
		}
	}

	/* 5. Move all Remaining entries from the existing target list to the new target list. */
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
	rte->inFromCl = false;
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

#if PG_VERSION_NUM >= 170000
	query->mergeTargetRelation = query->resultRelation;
#else
	query->mergeUseOuterJoin = true;
#endif
	query->targetList = NIL;
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg(
							"The on field specified in the $merge stage should be a string or an array containing strings, but a value of type %s was provided instead.",
							BsonTypeName(mergeArgs.on.value_type)),
						errdetail_log(
							"The on field specified in the $merge stage should be a string or an array containing strings, but a value of type %s was provided instead.",
							BsonTypeName(mergeArgs.on.value_type))));
	}

#if PG_VERSION_NUM >= 170000
	query->mergeJoinCondition = (Node *) make_ands_explicit(joinFilterList);
#else
	query->jointree->quals = (Node *) make_ands_explicit(joinFilterList);
#endif
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

		bson_iter_t indexKeyDocumentIter;
		pgbson *indexKeyDocument = indexDetail->indexSpec.indexKeyDocument;
		PgbsonInitIterator(indexKeyDocument, &indexKeyDocumentIter);

		if (keyNameIfSingleKeyJoin)
		{
			if (IsSingleUniqueIndexPresent(keyNameIfSingleKeyJoin,
										   &indexKeyDocumentIter))
			{
				foundRequiredIndex = true;
				break;
			}
		}
		else if (IsCompoundUniqueIndexPresent(onValues, &indexKeyDocumentIter,
											  numKeysOnField))
		{
			foundRequiredIndex = true;
			break;
		}
	}

	if (!foundRequiredIndex)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION51183),
						errmsg(
							"Unable to locate index required to confirm join fields are unique"),
						errdetail_log(
							"Unable to locate index required to confirm join fields are unique")));
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
IsSingleUniqueIndexPresent(const char *onValue, bson_iter_t *indexKeyDocumentIter)
{
	pgbsonelement uniqueIndexElement;

	/* if a document contains more than one element, it signifies a compound unique index, such as {"a" : 1, "b" : 1}. we should ignore that */
	if (TryGetSinglePgbsonElementFromBsonIterator(indexKeyDocumentIter,
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
 * - numElementsInMap: Number of elements in the 'onValues' array from which we will built hashmap.
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
							 bson_iter_t *indexKeyDocumentIter,
							 const int numElementsInMap)
{
	HTAB *onValueHashTable = InitHashTableFromStringArray(onValues, numElementsInMap);
	int foundCount = 0;

	/* make sure alll the elements in index document present in hash map */
	while (bson_iter_next(indexKeyDocumentIter))
	{
		StringView currentKey = bson_iter_key_string_view(indexKeyDocumentIter);
		bool foundInMap = false;
		hash_search(onValueHashTable, &currentKey, HASH_FIND, &foundInMap);

		if (!foundInMap)
		{
			hash_destroy(onValueHashTable);
			return false;
		}

		foundCount++;
	}

	hash_destroy(onValueHashTable);

	/* ensure that the map does not contain any elements beyond those in the index document */
	if (foundCount < numElementsInMap)
	{
		return false;
	}

	return true;
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
			inputArrayElement->value.v_utf8.len);

		bool found = false;
		hash_search(hashTable, &value, HASH_ENTER, &found);

		if (found)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION31465),
							errmsg(
								"Duplicate field %s detected", value.string),
							errdetail_log(
								"Duplicate field %s detected", value.string)));
		}
	}

	return hashTable;
}


/*
 * ValidatePreOutputStages traverse query tree to fail early if $merge/$out is used with $graphLookup or contains any mutable function.
 */
static inline void
ValidatePreOutputStages(Query *query, char *stageName)
{
	/* First, walk the query tree to detect any constructs that disqualify merge support */
	query_tree_walker(query, MergeQueryCTEWalker, stageName, 0);

	/* Already checked for known mutable functions; perform a recheck to ensure none were missed */
	if (contain_mutable_functions((Node *) query))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"The `%s` stage currently does not have support for use with mutable functions.",
							stageName),
						errdetail_log(
							"MUTABLE functions are not yet in MERGE command by citus")));
	}
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
		char *stageName = (char *) context;

		if (query->hasRecursive)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"$graphLookup is not supported with %s stage yet.",
								stageName),
							errdetail_log(
								"$graphLookup is not supported with $merge/$out stage yet.")));
		}

		query_tree_walker(query, MergeQueryCTEWalker, context, 0);

		/* we're done, no need to recurse anymore for this query */
		return false;
	}
	else if (IsA(node, FuncExpr))
	{
		FuncExpr *fexpr = (FuncExpr *) node;
		char *stageName = (char *) context;

		if (fexpr->funcid == ExtensionTableSampleSystemRowsFunctionId() ||
			fexpr->funcid == PgRandomFunctionOid())
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"The %s stage is not yet supported with the $sample aggregation stage.",
								stageName),
							errdetail_log(
								"MUTABLE functions are not yet in MERGE command by citus")));
		}
		else if (fexpr->funcid == BsonEmptyDataTableFunctionId())
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"The query references collections that do not exist. Create the missing collections and retry."),
							errdetail_log(
								"MUTABLE functions are not yet in MERGE command by citus")));
		}
	}

	return expression_tree_walker(node, MergeQueryCTEWalker, context);
}


/* let's validate final pgbson before writing to collection */
static inline void
ValidateFinalPgbsonBeforeWriting(const pgbson *finalBson, const pgbson *targetDocument,
								 ExprEvalState *stateForSchemaValidation, ValidationLevels
								 validationLevel)
{
	/* let's validate final document before insert */
	PgbsonValidateInputBson(finalBson, BSON_VALIDATE_NONE);
	if (finalBson != NULL)
	{
		uint32_t size = PgbsonGetBsonSize(finalBson);
		if (size > BSON_MAX_ALLOWED_SIZE)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BSONOBJECTTOOLARGE),
							errmsg("Size %u is larger than MaxDocumentSize %u",
								   size, BSON_MAX_ALLOWED_SIZE)));
		}
	}

	/* if we have stateForSchemaValidation, we need to validate the final document; */
	/* if validation level is moderate, final document could not match only if `targetDocument` also does not match */
	if (EnableSchemaValidation && stateForSchemaValidation != NULL)
	{
		ValidateSchemaOnDocumentUpdate(validationLevel, stateForSchemaValidation,
									   targetDocument, finalBson,
									   FAILED_VALIDATION_PLAN_EXECUTOR_ERROR_MSG);
	}
}


/*
 * This function Validate the ObjectId fields and write it in the writer.
 *
 * During validation, it addresses the following behavior:
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Something went wrong, Expecting object ID to be the first field in the target document"),
						errdetail_log(
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
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_IMMUTABLEFIELD),
							errmsg(
								"$merge could not update the target document as an immutable field (like '_id' or shardKey) was detected to be modified.")));
		}
	}

	/* Everything looks good, we can write the `_id` field to the writer */
	PgbsonWriterInit(writer);
	PgbsonWriterAppendValue(writer, "_id", 3, &objectIdFromTargetDocument.bsonValue);
}


/*
 * Mutates the query for the $out stage
 *
 * Example command : { $out: { "db": <>, "coll" : <> } }
 * targetDb with schema validation enabled as `'{ "a" : { "$type" : "int" } }`, we need to apply schema validation to the final document.
 * sql query :
 *
 * MERGE INTO ONLY ApiDataSchemaName.documents_3 documents_3
 * USING ( SELECT collection.document,
 *            '3'::bigint AS target_shard_key_value,
 *            ApiInternalSchemaName.bson_dollar_merge_generate_object_id(collection.document) AS generated_object_id
 *           FROM ApiDataSchemaName.documents_2 collection
 *          WHERE collection.shard_key_value = '2'::bigint) agg_stage_0
 *   ON documents_3.shard_key_value OPERATOR(pg_catalog.=) agg_stage_0.target_shard_key_value AND FALSE
 *   WHEN NOT MATCHED
 *    THEN INSERT (shard_key_value, object_id, document, creation_time)
 *     VALUES (agg_stage_0.target_shard_key_value, COALESCE(bson_get_value(agg_stage_0.document, '_id'::text), agg_stage_0.generated_object_id), ApiInternalSchemaName.bson_dollar_merge_add_object_id(agg_stage_0.document, agg_stage_0.generated_object_id, '{ "a" : { "$type" : "int" } }'::bson), '2024-08-21 11:06:38.323204+00'::timestamp with time zone)
 */
Query *
HandleOut(const bson_value_t *existingValue, Query *query,
		  AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_OUT);

	OutArgs outArgs = { 0 };

	memset(&outArgs, 0, sizeof(outArgs));
	ParseOutStage(existingValue, context->namespaceName, &outArgs);
	ValidatePreOutputStages(query, "$out");

	MongoCollection *targetCollection =
		GetMongoCollectionOrViewByNameDatum(StringViewGetTextDatum(&outArgs.targetDB),
											StringViewGetTextDatum(
												&outArgs.targetCollection),
											RowExclusiveLock);

	if (targetCollection)
	{
		bool isSourceSameAsTarget = targetCollection->collectionId ==
									context->mongoCollection->collectionId;

		if (targetCollection->viewDefinition != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTEDONVIEW),
							errmsg(
								"The namespace %s.%s refers to a view object rather than a collection",
								targetCollection->name.databaseName,
								targetCollection->name.collectionName),
							errdetail_log(
								"The namespace %s.%s refers to a view object rather than a collection",
								targetCollection->name.databaseName,
								targetCollection->name.collectionName)));
		}
		else if (targetCollection && targetCollection->shardKey != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION28769),
							errmsg("%s.%s cannot be sharded", outArgs.targetDB.string,
								   outArgs.targetCollection.string)));
		}
		else if (isSourceSameAsTarget)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg(
								"The target collection cannot be the same as the source collection in $out stage.")));
		}

		/* Truncate the target data table to delete all entries. This allows us to write new data into it */
		TruncateDataTable(targetCollection->collectionId);
	}
	else
	{
		bool isMergeStage = false;
		ValidateTargetNameSpaceForOutputStage(&outArgs.targetDB,
											  &outArgs.targetCollection, isMergeStage);

		targetCollection =
			CreateCollectionForInsert(StringViewGetTextDatum(&outArgs.targetDB),
									  StringViewGetTextDatum(&outArgs.targetCollection));
	}

	RearrangeTargetListForMerge(query, targetCollection, false, NULL);
	context->expandTargetList = true;
	query = MigrateQueryToSubQuery(query, context);
	query->commandType = CMD_MERGE;
	AddTargetCollectionRTEDollarMerge(query, targetCollection);

	/* If targetCollection enables schema validation, apply to target document*/
	bool bypassDocumentValidation = false;
	Const *schemaValidatorInfoConst = MakeBsonConst(PgbsonInitEmpty());
	if (CheckSchemaValidationEnabled(targetCollection, bypassDocumentValidation))
	{
		schemaValidatorInfoConst = MakeBsonConst(
			targetCollection->schemaValidator.validator);
	}

	/* Constant value assigned for source collection */
	const int sourceCollectionVarNo = 2;     /* In merge query source table is 2nd table */
	const int sourceDocAttrNo = 1;           /* In source table first projector is document */
	const int sourceShardKeyValueAttrNo = 2; /* we will append shard_key_value in source query at 2nd position after document column */
	const int generatedObjectIdAttrNo = 3;   /* we will append generated object_id in source query at 3rd position after shard_key_value column */
	const int targetCollectionVarNo = 1;     /* In merge query target table is 1st table */
	const int targetShardKeyValueAttrNo = 1; /* From Target table we are just selecting 3 columns first one is shard_key_value */

	Var *sourceDocVar = makeVar(sourceCollectionVarNo, sourceDocAttrNo,
								BsonTypeId(), -1,
								InvalidOid, 0);
	Var *sourceShardKeyValueVar = makeVar(sourceCollectionVarNo,
										  sourceShardKeyValueAttrNo, INT8OID, -1, 0, 0);
	Var *generatedObjectIdVar = makeVar(sourceCollectionVarNo,
										generatedObjectIdAttrNo, BsonTypeId(), -1, 0, 0);

	Var *targetShardKeyValueVar = makeVar(targetCollectionVarNo,
										  targetShardKeyValueAttrNo, INT8OID, -1, 0, 0);

	query->mergeActionList = list_make1(MakeActionWhenNotMatched(WhenNotMatched_INSERT,
																 sourceDocVar,
																 generatedObjectIdVar,
																 sourceShardKeyValueVar,
																 targetCollection,
																 schemaValidatorInfoConst));

	/* Write the join condition for $out, which will be in the form of
	 * `ON target.shard_key_value = source.target_shard_key_value`
	 * This is necessary because Citus requires the target's distributed column in the join condition.
	 * In any case, for $out, we always write to an empty table, so it's always whenNotMatched. */
	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 2;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

#if PG_VERSION_NUM >= 170000
	query->mergeJoinCondition = (Node *) make_opclause(PostgresInt4EqualOperatorOid(),
													   BOOLOID, false,
													   (Expr *) targetShardKeyValueVar,
													   (Expr *) sourceShardKeyValueVar,
													   InvalidOid,
													   InvalidOid);
#else
	query->jointree->quals = (Node *) make_opclause(PostgresInt4EqualOperatorOid(),
													BOOLOID, false,
													(Expr *) targetShardKeyValueVar,
													(Expr *) sourceShardKeyValueVar,
													InvalidOid,
													InvalidOid);
#endif
	return query;
}


/*
 * Truncate data table corresponding to the input collection id.
 */
static void
TruncateDataTable(int collectionId)
{
	StringInfo cmdStr = makeStringInfo();

	appendStringInfo(cmdStr,
					 "TRUNCATE TABLE %s.documents_%d",
					 ApiDataSchemaName, collectionId);

	bool isNull = false;
	bool readOnly = false;
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY, &isNull);
}


/*
 * Parses & validates the input $out spec.
 * first way :   { $out : "collection-name" }
 * second way : { $out : {"db" : "database-name", "coll" : "collection-name"} }
 *
 * Parsed outputs are placed in the OutArgs struct.
 */
static void
ParseOutStage(const bson_value_t *existingValue, const char *currentNameSpace,
			  OutArgs *args)
{
	if (existingValue->value_type != BSON_TYPE_DOCUMENT && existingValue->value_type !=
		BSON_TYPE_UTF8)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16990),
						errmsg(
							"Expected 'string' or 'document' type but found '%s' type",
							BsonTypeName(existingValue->value_type))));
	}

	if (existingValue->value_type == BSON_TYPE_UTF8)
	{
		char *dbName = NULL;
		char *dotPosition = strchr(currentNameSpace, '.');
		if (dotPosition != NULL)
		{
			size_t dbNameLength = dotPosition - currentNameSpace;
			dbName = pnstrdup(currentNameSpace, dbNameLength);
		}

		args->targetCollection = (StringView) {
			.length = existingValue->value.v_utf8.len,
			.string = existingValue->value.v_utf8.str
		};

		args->targetDB = CreateStringViewFromString(dbName);
		return;
	}

	/* parse when input is a document */
	bson_iter_t mergeIter;
	BsonValueInitIterator(existingValue, &mergeIter);

	while (bson_iter_next(&mergeIter))
	{
		const char *key = bson_iter_key(&mergeIter);
		const bson_value_t *bsonValue = bson_iter_value(&mergeIter);
		if (strcmp(key, "db") == 0)
		{
			if (bsonValue->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION13111),
								errmsg("wrong type for field (db) %s != string",
									   BsonTypeName(bsonValue->value_type))));
			}

			args->targetDB = (StringView) {
				.length = bsonValue->value.v_utf8.len,
				.string = bsonValue->value.v_utf8.str
			};
		}
		else if (strcmp(key, "coll") == 0)
		{
			if (bsonValue->value_type != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION13111),
								errmsg(
									"Field type mismatch in (coll): %s expected string",
									BsonTypeName(bsonValue->value_type))));
			}

			args->targetCollection = (StringView) {
				.length = bsonValue->value.v_utf8.len,
				.string = bsonValue->value.v_utf8.str
			};
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16990),
							errmsg(
								"If an object is passed to $out it must have exactly 2 fields: 'db' and 'coll'")));
		}
	}

	if (args->targetDB.length == 0 || args->targetCollection.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16994),
						errmsg(
							"If an object is passed to $out it must have exactly 2 fields: 'db' and 'coll'")));
	}
}


/* check whether to perform schema validation in stage $merge/$out */
static inline bool
CheckSchemaValidationEnabledForDollarMergeOut(void)
{
	return EnableSchemaValidation;
}


/*
 * Output stages can not write into db name : `config`, `local`, `admin`
 * Output stages can not write into collections starts from `system.`
 */
static inline void
ValidateTargetNameSpaceForOutputStage(const StringView *targetDB,
									  const StringView *targetCollection,
									  const bool isMergeStage)
{
	const char *stageName = isMergeStage ? "$merge" : "$out";

	if (StringViewEqualsCString(targetDB, "config") ||
		StringViewEqualsCString(targetDB, "local") ||
		StringViewEqualsCString(targetDB, "admin"))
	{
		int errorCode = isMergeStage ? ERRCODE_DOCUMENTDB_LOCATION31320 :
						ERRCODE_DOCUMENTDB_LOCATION31321;
		ereport(ERROR, (errcode(errorCode),
						errmsg("Unable to %s into internal database resource: %s",
							   stageName,
							   targetDB->string)));
	}

	const StringView SystemPrefix = { .length = 7, .string = "system." };
	if (StringViewStartsWithStringView(targetCollection, &SystemPrefix))
	{
		int errorCode = isMergeStage ? ERRCODE_DOCUMENTDB_LOCATION31319 :
						ERRCODE_DOCUMENTDB_LOCATION17385;
		ereport(ERROR, (errcode(errorCode),
						errmsg(" Unable to %s into designated special collection: %s",
							   stageName, targetCollection->string)));
	}
}
