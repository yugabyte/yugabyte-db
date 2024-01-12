/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/planner/bson_aggregation_metadata_queries.c
 *
 * Implementation of the backend query generation for metadata type queries
 * (e.g. listCollections, listIndexes).
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <float.h>
#include <fmgr.h>
#include <miscadmin.h>

#include <access/table.h>
#include <utils/rel.h>
#include <catalog/namespace.h>
#include <optimizer/planner.h>
#include <nodes/nodes.h>
#include <nodes/makefuncs.h>
#include <nodes/nodeFuncs.h>
#include <parser/parser.h>
#include <parser/parse_agg.h>
#include <parser/parse_clause.h>
#include <parser/parse_param.h>
#include <parser/analyze.h>
#include <parser/parse_oper.h>
#include <utils/ruleutils.h>
#include <utils/builtins.h>
#include <catalog/pg_aggregate.h>
#include <catalog/pg_class.h>
#include <parser/parsetree.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/lsyscache.h>

#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "query/query_operator.h"
#include "planner/pgmongo_planner.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "utils/feature_counter.h"
#include "customscan/pgmongo_custom_scan.h"
#include "utils/version_utils.h"

#include "aggregation/bson_aggregation_pipeline_private.h"

static Query * GenerateBaseListCollectionsQuery(Datum databaseDatum, bool nameOnly,
												AggregationPipelineBuildContext *context);
static Query * HandleListCollectionsProjector(Query *query,
											  AggregationPipelineBuildContext *context,
											  bool nameOnly);
static Query * GenerateBaseListIndexesQuery(Datum databaseDatum, const
											StringView *collectionName,
											AggregationPipelineBuildContext *context);

static Query * BuildSingleFunctionQuery(Oid queryFunctionOid, List *queryArgs, bool
										isMultiRow);


/*
 * Generates the base query for collection agnostic aggregate queries
 * and populates the necessary state into the context object.
 * The query formed is
 * SELECT NULL::bson;
 * This is because any agnostic stage will overwrite this anyway.
 */
Query *
GenerateBaseAgnosticQuery(Datum databaseDatum, AggregationPipelineBuildContext *context)
{
	StringView agnosticCollection = CreateStringViewFromString("$cmd.aggregate");
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	context->collectionNameView = agnosticCollection;
	context->namespaceName = CreateNamespaceName(DatumGetTextP(databaseDatum),
												 &agnosticCollection);
	context->mongoCollection = NULL;

	query->rtable = NIL;

	/* Create an empty jointree */
	query->jointree = makeNode(FromExpr);

	/* Create the projector. We only project the NULL::bson in this type of query */
	Const *documentEntry = makeConst(BsonTypeId(), -1, InvalidOid, -1, (Datum) 0, true,
									 false);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);
	return query;
}


/*
 * Generates a query that is akin to the MongoDB $listCollections query command
 */
Query *
GenerateListCollectionsQuery(Datum databaseDatum, pgbson *listCollectionsSpec,
							 QueryData *queryData, bool addCursorParams)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = databaseDatum;

	bson_iter_t listCollectionsIter;
	PgbsonInitIterator(listCollectionsSpec, &listCollectionsIter);

	bson_value_t filter = { 0 };
	bool nameOnly = false;

	while (bson_iter_next(&listCollectionsIter))
	{
		StringView keyView = bson_iter_key_string_view(&listCollectionsIter);
		const bson_value_t *value = bson_iter_value(&listCollectionsIter);

		if (StringViewEqualsCString(&keyView, "listCollections"))
		{
			continue;
		}
		else if (StringViewEqualsCString(&keyView, "filter"))
		{
			if (!BSON_ITER_HOLDS_NULL(&listCollectionsIter))
			{
				EnsureTopLevelFieldType("filter", &listCollectionsIter,
										BSON_TYPE_DOCUMENT);
				filter = *value;
			}
		}
		else if (StringViewEqualsCString(&keyView, "cursor"))
		{
			ParseCursorDocument(&listCollectionsIter, queryData);
		}
		else if (StringViewEqualsCString(&keyView, "nameOnly"))
		{
			nameOnly = BsonValueAsBool(value);
		}
		else if (StringViewEqualsCString(&keyView, "authorizedCollections"))
		{
			/* TODO: Handle this */
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(MongoUnknownBsonField),
							errmsg("BSON field listCollections.%.*s is an unknown field",
								   keyView.length, keyView.string),
							errhint("BSON field listCollections.%.*s is an unknown field",
									keyView.length, keyView.string)));
		}
	}

	Query *query = GenerateBaseListCollectionsQuery(databaseDatum, nameOnly, &context);
	queryData->namespaceName = context.namespaceName;

	query = HandleListCollectionsProjector(query, &context, nameOnly);

	/* apply match */
	if (filter.value_type != BSON_TYPE_EOD)
	{
		query = HandleMatch(&filter, query, &context);
		context.stageNum++;
	}

	return query;
}


/*
 * Generates a query that is akin to the MongoDB $listIndexes query command
 */
Query *
GenerateListIndexesQuery(Datum databaseDatum, pgbson *listIndexesSpec,
						 QueryData *queryData,
						 bool addCursorParams)
{
	AggregationPipelineBuildContext context = { 0 };
	context.databaseNameDatum = databaseDatum;

	bson_iter_t listIndexesIter;
	PgbsonInitIterator(listIndexesSpec, &listIndexesIter);

	StringView collectionName = { 0 };
	while (bson_iter_next(&listIndexesIter))
	{
		StringView keyView = bson_iter_key_string_view(&listIndexesIter);
		if (StringViewEqualsCString(&keyView, "listIndexes") ||
			StringViewEqualsCString(&keyView, "listindexes"))
		{
			EnsureTopLevelFieldType("listIndexes", &listIndexesIter, BSON_TYPE_UTF8);
			collectionName.string = bson_iter_utf8(&listIndexesIter,
												   &collectionName.length);
		}
		else if (StringViewEqualsCString(&keyView, "cursor"))
		{
			ParseCursorDocument(&listIndexesIter, queryData);
		}
		else if (!IsCommonSpecIgnoredField(keyView.string))
		{
			ereport(ERROR, (errcode(MongoUnknownBsonField),
							errmsg("BSON field listIndexes.%.*s is an unknown field",
								   keyView.length, keyView.string),
							errhint("BSON field listIndexes.%.*s is an unknown field",
									keyView.length, keyView.string)));
		}
	}

	Query *query = GenerateBaseListIndexesQuery(databaseDatum, &collectionName, &context);
	queryData->namespaceName = context.namespaceName;
	return query;
}


/*
 * Mutates the query to process the CurrentOp aggregation stage
 * Stage parameters:
 * { allUsers: <boolean>, idleConnections: <boolean>, idleCursors: <boolean>, idleSessions: <boolean>, localOps: <boolean> }
 * This stage will form the query
 * SELECT document FROM mongo_api_v1.current_op_aggregation({ currentOpSpec });
 * Requires this to be the first stage, so the prior query is discarded.
 */
Query *
HandleCurrentOp(const bson_value_t *existingValue, Query *query,
				AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_CURRENTOP);
	EnsureTopLevelFieldValueType("pipeline.$currentOp", existingValue,
								 BSON_TYPE_DOCUMENT);

	if (context->stageNum != 0)
	{
		ereport(ERROR, (errcode(MongoLocation40602),
						errmsg(
							"$currentOp is only valid as the first stage in the pipeline.")));
	}

	const char *databaseStr = TextDatumGetCString(context->databaseNameDatum);
	if (strcmp(databaseStr, "admin") != 0 ||
		query->jointree->fromlist != NULL)
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg(
							"$currentOp must be run against the 'admin' database with {aggregate: 1}")));
	}

	/* Any further validation done during processing of the currentOp aggregation */
	Assert(query->jointree->fromlist == NULL);
	Assert(query->rtable == NIL);

	/* Now create a function RTE */
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	rte->rtekind = RTE_FUNCTION;
	rte->relid = InvalidOid;

	List *colNames = list_make1(makeString("document"));
	rte->alias = rte->eref = makeAlias("currentOp", colNames);
	rte->lateral = false;
	rte->inFromCl = true;
	rte->functions = NIL;
	rte->inh = false;
	rte->requiredPerms = ACL_SELECT;
	rte->rellockmode = AccessShareLock;

	/* Now create the rtfunc*/
	List *args = list_make1(MakeBsonConst(PgbsonInitFromDocumentBsonValue(
											  existingValue)));
	FuncExpr *rangeFunc = makeFuncExpr(BsonCurrentOpAggregationFunctionId(), BsonTypeId(),
									   args,
									   InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	rangeFunc->funcretset = true;
	RangeTblFunction *rangeTableFunction = makeNode(RangeTblFunction);
	rangeTableFunction->funccolcount = 4;
	rangeTableFunction->funccolnames = colNames;
	rangeTableFunction->funccoltypes = list_make4_oid(INT8OID, BsonTypeId(),
													  BsonTypeId(), TIMESTAMPTZOID);
	rangeTableFunction->funccoltypmods = NIL;
	rangeTableFunction->funccolcollations = list_make4_oid(InvalidOid, InvalidOid,
														   InvalidOid, InvalidOid);
	rangeTableFunction->funcparams = NULL;
	rangeTableFunction->funcexpr = (Node *) rangeFunc;

	/* Add the RTFunc to the RTE */
	rte->functions = list_make1(rangeTableFunction);

	query->rtable = list_make1(rte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	/* Create the projector. We only project the 'document' column in this type of query */
	Var *documentEntry = makeVar(1, 1, BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	return query;
}


/*
 * Generates the base query that queries the mongo_api_v1.list_indexes
 * for a listIndexes scenario.
 * TODO: Move this to directly query the table (so we can do cursors).
 */
static Query *
GenerateBaseListIndexesQuery(Datum databaseDatum, const StringView *collectionName,
							 AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	context->namespaceName = CreateNamespaceName(DatumGetTextP(databaseDatum),
												 collectionName);
	context->collectionNameView = *collectionName;

	RangeTblEntry *rte = makeNode(RangeTblEntry);

	/* Match spec for mongo_api_v1.list_indexes function */
	List *colNames = list_make1(makeString("list_indexes"));

	rte->rtekind = RTE_FUNCTION;
	rte->relid = InvalidOid;

	rte->alias = rte->eref = makeAlias("list_indexes", colNames);
	rte->lateral = false;
	rte->inFromCl = true;
	rte->functions = NIL;
	rte->inh = false;
	rte->requiredPerms = ACL_SELECT;
	rte->rellockmode = AccessShareLock;

	/* Now create the rtfunc*/
	List *args = list_make2(makeConst(TEXTOID, -1, InvalidOid, -1, databaseDatum, false,
									  false),
							MakeTextConst(collectionName->string,
										  collectionName->length));
	FuncExpr *rangeFunc = makeFuncExpr(ApiListIndexesFunctionId(), BsonTypeId(),
									   args, InvalidOid, InvalidOid,
									   COERCE_EXPLICIT_CALL);
	rangeFunc->funcretset = true;

	RangeTblFunction *rangeTableFunction = makeNode(RangeTblFunction);
	rangeTableFunction->funccolcount = 1;
	rangeTableFunction->funccolnames = colNames;
	rangeTableFunction->funccoltypes = list_make1_oid(BsonTypeId());
	rangeTableFunction->funccoltypmods = NIL;
	rangeTableFunction->funccolcollations = list_make1_oid(InvalidOid);
	rangeTableFunction->funcparams = NULL;
	rangeTableFunction->funcexpr = (Node *) rangeFunc;

	/* Add the RTFunc to the RTE */
	rte->functions = list_make1(rangeTableFunction);

	query->rtable = list_make1(rte);

	/* Now register the RTE in the "FROM" clause with a single filter on the database_name */
	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	/* Add a result projector bson document */
	Var *rowExpr = makeVar(1, 1, BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) rowExpr, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	return query;
}


/*
 * Generates the base table that queries the ApiCatalogSchemaName.collections
 * for a listCollections scenario.
 */
static Query *
GenerateBaseListCollectionsQuery(Datum databaseDatum, bool nameOnly,
								 AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	StringView collectionsView = CreateStringViewFromString("$cmd.ListCollections");
	context->namespaceName = CreateNamespaceName(DatumGetTextP(databaseDatum),
												 &collectionsView);
	context->collectionNameView = collectionsView;

	RangeTblEntry *rte = makeNode(RangeTblEntry);

	/* Match spec for mongo_cataloc.collections function */
	List *colNames = list_concat(list_make3(makeString("database_name"), makeString(
												"collection_name"), makeString(
												"collection_id")),
								 list_make3(makeString("shard_key"), makeString(
												"collection_uuid"), makeString(
												"view_definition")));
	rte->rtekind = RTE_RELATION;
	rte->alias = rte->eref = makeAlias("collection", colNames);
	rte->lateral = false;
	rte->inFromCl = true;
	rte->relkind = RELKIND_RELATION;
	rte->functions = NIL;
	rte->inh = true;
	rte->requiredPerms = ACL_SELECT;
	rte->rellockmode = AccessShareLock;

	RangeVar *rangeVar = makeRangeVar(ApiCatalogSchemaName, "collections", -1);
	rte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);

	query->rtable = list_make1(rte);

	/* Now register the RTE in the "FROM" clause with a single filter on the database_name */
	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;

	Var *databaseVar = makeVar(1, 1, TEXTOID, -1, InvalidOid, 0);
	Expr *opExpr = make_opclause(TextEqualOperatorId(), BOOLOID, false,
								 (Expr *) databaseVar,
								 (Expr *) makeConst(TEXTOID, -1, InvalidOid, -1,
													databaseDatum, false, false),
								 InvalidOid, InvalidOid);
	query->jointree = makeFromExpr(list_make1(rtr), (Node *) opExpr);

	/* Add a row_get_bson to make it a single bson document */
	Var *rowExpr = makeVar(1, 0, MongoCatalogCollectionsTypeOid(), -1, InvalidOid, 0);
	FuncExpr *funcExpr = makeFuncExpr(RowGetBsonFunctionOid(), BsonTypeId(),
									  list_make1(rowExpr), InvalidOid, InvalidOid,
									  COERCE_EXPLICIT_CALL);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) funcExpr, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	return query;
}


/*
 * Modifies the query to handle the $collStats stage.
 */
Query *
HandleCollStats(const bson_value_t *existingValue, Query *query,
				AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_COLLSTATS);
	EnsureTopLevelFieldValueType("$collStats", existingValue, BSON_TYPE_DOCUMENT);

	if (context->stageNum != 0)
	{
		ereport(ERROR, (errcode(MongoLocation40602),
						errmsg(
							"$collStats is only valid as the first stage in the pipeline.")));
	}

	/* Skip validate the collStats document: done in the function */
	/* Now create the rtfunc*/
	Const *databaseConst = makeConst(TEXTOID, -1, InvalidOid, -1,
									 context->databaseNameDatum, false, false);
	Const *collectionConst = MakeTextConst(context->collectionNameView.string,
										   context->collectionNameView.length);
	pgbson *bson = PgbsonInitFromDocumentBsonValue(existingValue);
	List *collStatsArgs = list_make3(databaseConst, collectionConst, MakeBsonConst(bson));

	/* Remove the collection (it's not on the base table) */
	context->mongoCollection = NULL;
	bool isMultiRow = false;
	return BuildSingleFunctionQuery(ApiCollStatsAggregationFunctionOid(),
									collStatsArgs, isMultiRow);
}


/*
 * Modifies the query to handle the $indexStats stage.
 */
Query *
HandleIndexStats(const bson_value_t *existingValue, Query *query,
				 AggregationPipelineBuildContext *context)
{
	ReportFeatureUsage(FEATURE_STAGE_INDEXSTATS);
	EnsureTopLevelFieldValueType("$indexStats", existingValue, BSON_TYPE_DOCUMENT);

	if (!IsBsonValueEmptyDocument(existingValue))
	{
		ereport(ERROR, (errcode(MongoLocation28803),
						errmsg(
							"The $indexStats stage specification must be an empty object")));
	}

	if (context->stageNum != 0)
	{
		ereport(ERROR, (errcode(MongoLocation40602),
						errmsg(
							"$indexStats is only valid as the first stage in the pipeline.")));
	}

	Const *databaseConst = makeConst(TEXTOID, -1, InvalidOid, -1,
									 context->databaseNameDatum, false, false);
	Const *collectionConst = MakeTextConst(context->collectionNameView.string,
										   context->collectionNameView.length);
	List *indexStatsArgs = list_make2(databaseConst, collectionConst);

	/* Remove the collection (it's not on the base table) */
	context->mongoCollection = NULL;

	bool isMultiRow = true;
	return BuildSingleFunctionQuery(ApiIndexStatsAggregationFunctionOid(),
									indexStatsArgs, isMultiRow);
}


/*
 * Builds a single query that is the equivalent of
 * SELECT document FROM queryFunction(args);
 */
static Query *
BuildSingleFunctionQuery(Oid queryFunctionOid, List *queryArgs, bool isMultiRow)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	List *colNames = list_make1(makeString("document"));
	RangeTblEntry *rte = makeNode(RangeTblEntry);
	rte->rtekind = RTE_FUNCTION;
	rte->relid = InvalidOid;

	rte->alias = rte->eref = makeAlias("collection", colNames);
	rte->lateral = false;
	rte->inFromCl = true;
	rte->functions = NIL;
	rte->inh = false;
	rte->requiredPerms = ACL_SELECT;
	rte->rellockmode = AccessShareLock;

	/* Now create the rtfunc*/
	FuncExpr *rangeFunc = makeFuncExpr(queryFunctionOid, BsonTypeId(), queryArgs,
									   InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	if (isMultiRow)
	{
		rangeFunc->funcretset = true;
	}

	RangeTblFunction *rangeTableFunction = makeNode(RangeTblFunction);
	rangeTableFunction->funccolcount = 1;
	rangeTableFunction->funccolnames = NIL;
	rangeTableFunction->funccoltypes = list_make1_oid(BsonTypeId());
	rangeTableFunction->funccoltypmods = list_make1_int(-1);
	rangeTableFunction->funccolcollations = list_make1_oid(InvalidOid);
	rangeTableFunction->funcparams = NULL;
	rangeTableFunction->funcexpr = (Node *) rangeFunc;

	/* Add the RTFunc to the RTE */
	rte->functions = list_make1(rangeTableFunction);

	query->rtable = list_make1(rte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), NULL);

	Var *documentEntry = makeVar(1, 1, BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);
	return query;
}


/* writes the condition
 * path: { "$cond": [ { "$toBool": "$view_definition" }, value1, value2 ]}
 */
static void
WriteConditionWithIfViewsNull(pgbson_writer *writer,
							  const char *path, uint32_t pathLength,
							  const bson_value_t *trueValue,
							  const bson_value_t *falseValue)
{
	pgbson_writer childWriter;
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartDocument(writer, path, pathLength, &childWriter);
	PgbsonWriterStartArray(&childWriter, "$cond", 5, &arrayWriter);

	pgbson_writer toBoolWriter;
	PgbsonArrayWriterStartDocument(&arrayWriter, &toBoolWriter);
	PgbsonWriterAppendUtf8(&toBoolWriter, "$toBool", 7, "$view_definition");
	PgbsonArrayWriterEndDocument(&arrayWriter, &toBoolWriter);

	PgbsonArrayWriterWriteValue(&arrayWriter, trueValue);
	PgbsonArrayWriterWriteValue(&arrayWriter, falseValue);

	PgbsonWriterEndArray(&childWriter, &arrayWriter);
	PgbsonWriterEndDocument(writer, &childWriter);
}


/*
 * Modifies the ListCollections base table to match the mongo syntax.
 */
static Query *
HandleListCollectionsProjector(Query *query, AggregationPipelineBuildContext *context,
							   bool nameOnly)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	/* "name": "collection_name "*/
	PgbsonWriterAppendUtf8(&writer, "name", 4, "$collection_name");

	/* "type": { "$cond": [ { "$toBool": "$view_definition" }, "view", "collection" ]} */
	{
		bson_value_t collectionValue = { 0 };
		collectionValue.value_type = BSON_TYPE_UTF8;
		collectionValue.value.v_utf8.str = "collection";
		collectionValue.value.v_utf8.len = 10;
		bson_value_t viewValue = { 0 };
		viewValue.value_type = BSON_TYPE_UTF8;
		viewValue.value.v_utf8.str = "view";
		viewValue.value.v_utf8.len = 4;

		WriteConditionWithIfViewsNull(&writer, "type", 4, &viewValue, &collectionValue);
	}

	if (!nameOnly)
	{
		/* "options": { "$cond": [ { "$toBool": "$view_definition" }, "$view_definition", {} ] } */
		pgbson *doc = PgbsonInitEmpty();
		bson_value_t collectionValue = ConvertPgbsonToBsonValue(doc);
		bson_value_t viewValue = { 0 };
		viewValue.value_type = BSON_TYPE_UTF8;
		viewValue.value.v_utf8.str = "$view_definition";
		viewValue.value.v_utf8.len = 16;

		WriteConditionWithIfViewsNull(&writer, "options", 7, &viewValue,
									  &collectionValue);

		/* "info": { "readOnly": { "$ifNull": [ { "$toBool": "$view_definition"}, false] }, "uuid": "$collection_uuid", "shardKey": "$shard_key" } } */
		pgbson_writer infoWriter;
		PgbsonWriterStartDocument(&writer, "info", 4, &infoWriter);

		pgbson_writer readOnlyWriter;
		PgbsonWriterStartDocument(&infoWriter, "readOnly", 8, &readOnlyWriter);

		pgbson_array_writer ifNullWriter;
		PgbsonWriterStartArray(&readOnlyWriter, "$ifNull", 7, &ifNullWriter);

		pgbson_writer toBoolWriter;
		PgbsonArrayWriterStartDocument(&ifNullWriter, &toBoolWriter);
		PgbsonWriterAppendUtf8(&toBoolWriter, "$toBool", 7, "$view_definition");
		PgbsonArrayWriterEndDocument(&ifNullWriter, &toBoolWriter);

		bson_value_t falseValue = { 0 };
		falseValue.value_type = BSON_TYPE_BOOL;
		falseValue.value.v_bool = false;
		PgbsonArrayWriterWriteValue(&ifNullWriter, &falseValue);

		PgbsonWriterEndArray(&readOnlyWriter, &ifNullWriter);
		PgbsonWriterEndDocument(&infoWriter, &readOnlyWriter);

		PgbsonWriterAppendUtf8(&infoWriter, "uuid", 4, "$collection_uuid");
		PgbsonWriterAppendUtf8(&infoWriter, "shardKey", 8, "$shard_key");

		PgbsonWriterEndDocument(&writer, &infoWriter);

		/* "idIndex": { "$cond": [ { "$toBool": "$view_definition" }, null, { "v": 2, "key": { "_id": 1 }, "name": "_id_" } ] } */
		pgbson_writer idIndexWriter;
		pgbson_writer keyWriter;
		PgbsonWriterInit(&idIndexWriter);
		PgbsonWriterAppendInt32(&idIndexWriter, "v", 1, 2);
		PgbsonWriterAppendUtf8(&idIndexWriter, "name", 4, "_id_");
		PgbsonWriterStartDocument(&idIndexWriter, "key", 3, &keyWriter);
		PgbsonWriterAppendInt32(&keyWriter, "_id", 3, 1);
		PgbsonWriterEndDocument(&idIndexWriter, &keyWriter);

		collectionValue = ConvertPgbsonToBsonValue(PgbsonWriterGetPgbson(
													   &idIndexWriter));
		viewValue = (bson_value_t) {
			0
		};
		viewValue.value_type = BSON_TYPE_NULL;

		WriteConditionWithIfViewsNull(&writer, "idIndex", 7, &viewValue,
									  &collectionValue);
	}

	pgbson *bson = PgbsonWriterGetPgbson(&writer);
	bson_value_t bsonValue = ConvertPgbsonToBsonValue(bson);
	return HandleSimpleProjectionStage(&bsonValue, query, context, "$project",
									   BsonDollarProjectFunctionOid());
}
