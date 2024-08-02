/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_aggregation_config_queries.c
 *
 * Implementation of the backend query generation for queries targetting
 * the config database.
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <float.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <catalog/pg_class.h>
#include <parser/parse_node.h>
#include <nodes/params.h>
#include <utils/builtins.h>
#include <catalog/namespace.h>
#include <parser/parse_relation.h>

#include "io/helio_bson_core.h"
#include "metadata/metadata_cache.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "aggregation/bson_aggregation_pipeline_private.h"
#include "api_hooks.h"

static Query * GenerateVersionQuery(AggregationPipelineBuildContext *context);
static Query * GenerateDatabasesQuery(AggregationPipelineBuildContext *context);
static Query * GenerateCollectionsQuery(AggregationPipelineBuildContext *context);
static Query * GenerateChunksQuery(AggregationPipelineBuildContext *context);
static Query * GenerateShardsQuery(AggregationPipelineBuildContext *context);
static Query * GenerateSettingsQuery(AggregationPipelineBuildContext *context);

/*
 * Sets the RTE of a table in the Config database.
 */
Query *
GenerateConfigDatabaseQuery(AggregationPipelineBuildContext *context)
{
	if (StringViewEqualsCString(&context->collectionNameView, "version"))
	{
		return GenerateVersionQuery(context);
	}
	else if (StringViewEqualsCString(&context->collectionNameView, "databases"))
	{
		context->requiresPersistentCursor = true;
		return GenerateDatabasesQuery(context);
	}
	else if (StringViewEqualsCString(&context->collectionNameView, "collections"))
	{
		context->requiresPersistentCursor = true;
		return GenerateCollectionsQuery(context);
	}
	else if (StringViewEqualsCString(&context->collectionNameView, "chunks"))
	{
		context->requiresPersistentCursor = true;
		return GenerateChunksQuery(context);
	}
	else if (StringViewEqualsCString(&context->collectionNameView, "settings"))
	{
		context->requiresPersistentCursor = true;
		return GenerateSettingsQuery(context);
	}
	else if (StringViewEqualsCString(&context->collectionNameView, "_shards"))
	{
		/* TODO: We can't enable this on shards because there's a dependency */
		/* on the "host" which requires a connection string. Once we can pass */
		/* the MX connection string - reconsider adding this back. */
		context->requiresPersistentCursor = true;
		return GenerateShardsQuery(context);
	}
	else
	{
		return NULL;
	}
}


/*
 * Generates a query that mimics the output of config.versions
 */
static Query *
GenerateVersionQuery(AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	context->mongoCollection = NULL;

	query->rtable = NIL;

	/* Create an empty jointree */
	query->jointree = makeNode(FromExpr);

	/* Create the projector. We only project the NULL::bson in this type of query */
	pgbson_writer versionsWriter;
	PgbsonWriterInit(&versionsWriter);
	PgbsonWriterAppendBool(&versionsWriter, "shardingEnabled", 15, true);

	Const *documentEntry = MakeBsonConst(PgbsonWriterGetPgbson(&versionsWriter));
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);
	context->requiresPersistentCursor = true;

	query = MigrateQueryToSubQuery(query, context);
	return query;
}


/*
 * Mimics the output of the config.databases collection.
 */
static Query *
GenerateDatabasesQuery(AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	RangeTblEntry *rte = makeNode(RangeTblEntry);

	/* Match spec for ApiCatalogSchemaName.collections function */
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
	rte->rellockmode = AccessShareLock;

	RangeVar *rangeVar = makeRangeVar(ApiCatalogSchemaName, "collections", -1);
	rte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);

#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *permInfo = addRTEPermissionInfo(&query->rteperminfos, rte);
	permInfo->requiredPerms = ACL_SELECT;
#else
	rte->requiredPerms = ACL_SELECT;
#endif
	query->rtable = list_make1(rte);

	/* Now register the RTE in the "FROM" clause with a single filter on shard_key not null */
	NullTest *nullTest = makeNode(NullTest);
	nullTest->argisrow = false;
	nullTest->nulltesttype = IS_NOT_NULL;
	nullTest->arg = (Expr *) makeVar(1, 4, BsonTypeId(), -1, InvalidOid, 0);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), (Node *) nullTest);

	/* Add a row_get_bson to make it a single bson document */
	Var *rowExpr = makeVar(1, 0, MongoCatalogCollectionsTypeOid(), -1, InvalidOid, 0);
	FuncExpr *funcExpr = makeFuncExpr(RowGetBsonFunctionOid(), BsonTypeId(),
									  list_make1(rowExpr), InvalidOid, InvalidOid,
									  COERCE_EXPLICIT_CALL);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) funcExpr, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	/* Move to a subquery */
	query = MigrateQueryToSubQuery(query, context);

	/* Now group by database_name */
	pgbson_writer groupWriter;
	PgbsonWriterInit(&groupWriter);
	PgbsonWriterAppendUtf8(&groupWriter, "_id", 3, "$database_name");
	pgbson *groupSpec = PgbsonWriterGetPgbson(&groupWriter);
	bson_value_t groupValue = ConvertPgbsonToBsonValue(groupSpec);
	query = HandleGroup(&groupValue, query, context);
	query = MigrateQueryToSubQuery(query, context);

	pgbson_writer projectionSpec;
	PgbsonWriterInit(&projectionSpec);
	PgbsonWriterAppendBool(&projectionSpec, "partitioned", 11, true);

	pgbson *spec = PgbsonWriterGetPgbson(&projectionSpec);
	bson_value_t projectionValue = ConvertPgbsonToBsonValue(spec);

	query = HandleSimpleProjectionStage(
		&projectionValue, query, context, "$addFields", BsonDollarAddFieldsFunctionOid(),
		NULL);

	return query;
}


/*
 * Mimics the output of the config.collections collection.
 */
static Query *
GenerateCollectionsQuery(AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	RangeTblEntry *rte = makeNode(RangeTblEntry);

	/* Match spec for ApiCatalogSchemaName.collections function */
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
	rte->rellockmode = AccessShareLock;

	RangeVar *rangeVar = makeRangeVar(ApiCatalogSchemaName, "collections", -1);
	rte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);

#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *permInfo = addRTEPermissionInfo(&query->rteperminfos, rte);
	permInfo->requiredPerms = ACL_SELECT;
#else
	rte->requiredPerms = ACL_SELECT;
#endif
	query->rtable = list_make1(rte);

	/* Now register the RTE in the "FROM" clause with a single filter on shard_key not null */
	NullTest *nullTest = makeNode(NullTest);
	nullTest->argisrow = false;
	nullTest->nulltesttype = IS_NOT_NULL;
	nullTest->arg = (Expr *) makeVar(1, 4, BsonTypeId(), -1, InvalidOid, 0);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), (Node *) nullTest);

	/* Add a row_get_bson to make it a single bson document */
	Var *rowExpr = makeVar(1, 0, MongoCatalogCollectionsTypeOid(), -1, InvalidOid, 0);
	FuncExpr *funcExpr = makeFuncExpr(RowGetBsonFunctionOid(), BsonTypeId(),
									  list_make1(rowExpr), InvalidOid, InvalidOid,
									  COERCE_EXPLICIT_CALL);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) funcExpr, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	/* Modify the output to match the config.collections output */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_writer childWriter;
	PgbsonWriterStartDocument(&writer, "_id", 3, &childWriter);

	pgbson_array_writer childArray;
	PgbsonWriterStartArray(&childWriter, "$concat", 7, &childArray);
	PgbsonArrayWriterWriteUtf8(&childArray, "$database_name");
	PgbsonArrayWriterWriteUtf8(&childArray, ".");
	PgbsonArrayWriterWriteUtf8(&childArray, "$collection_name");
	PgbsonWriterEndArray(&childWriter, &childArray);
	PgbsonWriterEndDocument(&writer, &childWriter);

	PgbsonWriterAppendUtf8(&writer, "key", 3, "$shard_key");

	/* Since we use $project, use $literal since bools and numbers need to be escaped */
	pgbson_writer expressionWriter;
	PgbsonWriterStartDocument(&writer, "noBalance", 9, &expressionWriter);
	PgbsonWriterAppendBool(&expressionWriter, "$literal", -1, true);
	PgbsonWriterEndDocument(&writer, &expressionWriter);

	pgbson *spec = PgbsonWriterGetPgbson(&writer);
	bson_value_t projectionValue = ConvertPgbsonToBsonValue(spec);

	query = HandleSimpleProjectionStage(
		&projectionValue, query, context, "$project", BsonDollarProjectFunctionOid(),
		NULL);

	return query;
}


/* Simulates the output of the config.chunks table */
static Query *
GenerateChunksQuery(AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	RangeTblEntry *rte = makeNode(RangeTblEntry);

	/* Match spec for ApiCatalogSchemaName.collections function */
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
	rte->rellockmode = AccessShareLock;

	RangeVar *rangeVar = makeRangeVar(ApiCatalogSchemaName, "collections", -1);
	rte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);

#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *permInfo = addRTEPermissionInfo(&query->rteperminfos, rte);
	permInfo->requiredPerms = ACL_SELECT;
#else
	rte->requiredPerms = ACL_SELECT;
#endif
	query->rtable = list_make1(rte);

	/* Now register the RTE in the "FROM" clause with a single filter on shard_key not null */
	NullTest *nullTest = makeNode(NullTest);
	nullTest->argisrow = false;
	nullTest->nulltesttype = IS_NOT_NULL;
	nullTest->arg = (Expr *) makeVar(1, 4, BsonTypeId(), -1, InvalidOid, 0);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	query->jointree = makeFromExpr(list_make1(rtr), (Node *) nullTest);

	/* Add a row_get_bson to make it a single bson document */
	Var *rowExpr = makeVar(1, 0, MongoCatalogCollectionsTypeOid(), -1, InvalidOid, 0);
	FuncExpr *funcExpr = makeFuncExpr(RowGetBsonFunctionOid(), BsonTypeId(),
									  list_make1(rowExpr), InvalidOid, InvalidOid,
									  COERCE_EXPLICIT_CALL);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) funcExpr, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);

	/* Modify the output to match the config.chunks output */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_writer childWriter;
	PgbsonWriterStartDocument(&writer, "ns", 2, &childWriter);

	pgbson_array_writer childArray;
	PgbsonWriterStartArray(&childWriter, "$concat", 7, &childArray);
	PgbsonArrayWriterWriteUtf8(&childArray, "$database_name");
	PgbsonArrayWriterWriteUtf8(&childArray, ".");
	PgbsonArrayWriterWriteUtf8(&childArray, "$collection_name");
	PgbsonWriterEndArray(&childWriter, &childArray);
	PgbsonWriterEndDocument(&writer, &childWriter);

	PgbsonWriterAppendUtf8(&writer, "shard", 5, "defaultShard");

	/* Since we use $project, use $literal since bools and numbers need to be escaped */
	pgbson_writer expressionWriter;
	PgbsonWriterStartDocument(&writer, "min", 3, &expressionWriter);
	PgbsonWriterAppendInt64(&expressionWriter, "$literal", -1, LONG_MIN);
	PgbsonWriterEndDocument(&writer, &expressionWriter);

	PgbsonWriterStartDocument(&writer, "max", 3, &expressionWriter);
	PgbsonWriterAppendInt64(&expressionWriter, "$literal", -1, LONG_MAX);
	PgbsonWriterEndDocument(&writer, &expressionWriter);


	pgbson *spec = PgbsonWriterGetPgbson(&writer);
	bson_value_t projectionValue = ConvertPgbsonToBsonValue(spec);

	query = HandleSimpleProjectionStage(
		&projectionValue, query, context, "$project", BsonDollarProjectFunctionOid(),
		NULL);

	return MutateChunksQueryForDistribution(query);
}


static Query *
GenerateShardsQuery(AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	context->mongoCollection = NULL;

	query->rtable = NIL;

	/* Create an empty jointree */
	query->jointree = makeNode(FromExpr);

	/* Create the projector. We only project the NULL::bson in this type of query */
	pgbson_writer shardsWriter;
	PgbsonWriterInit(&shardsWriter);
	PgbsonWriterAppendUtf8(&shardsWriter, "_id", 3, "defaultShard");

	Const *documentEntry = MakeBsonConst(PgbsonWriterGetPgbson(&shardsWriter));
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);
	context->requiresPersistentCursor = true;

	query = MigrateQueryToSubQuery(query, context);
	return MutateShardsQueryForDistribution(query);
}


static Query *
GenerateSettingsQuery(AggregationPipelineBuildContext *context)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;
	context->mongoCollection = NULL;

	List *valuesList = NIL;

	/* { _id: balancer, stopped: true } */
	pgbson_writer balancerWriter;
	PgbsonWriterInit(&balancerWriter);
	PgbsonWriterAppendUtf8(&balancerWriter, "_id", 3, "balancer");
	PgbsonWriterAppendBool(&balancerWriter, "stopped", 7, true);
	valuesList = lappend(valuesList, list_make1(MakeBsonConst(PgbsonWriterGetPgbson(
																  &balancerWriter))));

	/* { _id: autosplit, enabled: false } */
	pgbson_writer autosplitWriter;
	PgbsonWriterInit(&autosplitWriter);
	PgbsonWriterAppendUtf8(&autosplitWriter, "_id", 3, "autosplit");
	PgbsonWriterAppendBool(&autosplitWriter, "enabled", 7, false);
	valuesList = lappend(valuesList, list_make1(MakeBsonConst(PgbsonWriterGetPgbson(
																  &autosplitWriter))));

	RangeTblEntry *valuesRte = makeNode(RangeTblEntry);
	valuesRte->rtekind = RTE_VALUES;
	valuesRte->alias = valuesRte->eref = makeAlias("values", list_make1(makeString(
																			"document")));
	valuesRte->lateral = false;
	valuesRte->values_lists = valuesList;
	valuesRte->inh = false;
	valuesRte->inFromCl = true;

	valuesRte->coltypes = list_make1_oid(INT8OID);
	valuesRte->coltypmods = list_make1_int(-1);
	valuesRte->colcollations = list_make1_oid(InvalidOid);
	query->rtable = list_make1(valuesRte);

	query->jointree = makeNode(FromExpr);
	RangeTblRef *valuesRteRef = makeNode(RangeTblRef);
	valuesRteRef->rtindex = 1;
	query->jointree->fromlist = list_make1(valuesRteRef);

	/* Point to the values RTE */
	Var *documentEntry = makeVar(1, 1, BsonTypeId(), -1, InvalidOid, 0);
	TargetEntry *baseTargetEntry = makeTargetEntry((Expr *) documentEntry, 1, "document",
												   false);
	query->targetList = list_make1(baseTargetEntry);
	context->requiresPersistentCursor = true;

	return query;
}
