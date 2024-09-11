/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/colocation/shard_colocation.c
 *
 * Implementation of colocation and distributed placement for the extension.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <utils/builtins.h>
#include <parser/parse_node.h>
#include <parser/parse_relation.h>
#include <catalog/namespace.h>
#include <nodes/makefuncs.h>
#include <catalog/pg_collation.h>
#include <utils/fmgroids.h>
#include <utils/version_utils.h>
#include <utils/inval.h>
#include <parser/parse_func.h>

#include "io/helio_bson_core.h"
#include "utils/helio_errors.h"
#include "utils/query_utils.h"

#include "metadata/metadata_cache.h"
#include "metadata/collection.h"
#include "shard_colocation.h"
#include "api_hooks_def.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "aggregation/bson_aggregation_pipeline_private.h"

extern bool EnableNativeColocation;

PG_FUNCTION_INFO_V1(command_get_shard_map);
PG_FUNCTION_INFO_V1(command_list_shards);


/*
 * Metadata collected about Citus nodes.
 * This is the output that we care about collected from
 * Citus's pg_dist_node.
 */
typedef struct NodeInfo
{
	/*
	 * The groupId for the node. This has 1 per
	 * server (Coordinator, Worker1, Worker2)
	 */
	int32_t groupId;

	/*
	 * An id for the specific node
	 */
	int32_t nodeId;

	/*
	 * The citus role for the node, be it
	 * Primary or secondary (for replicas)
	 */
	const char *nodeRole;

	/*
	 * The name of the cluster: Default for writes
	 * and the clustername for reads.
	 */
	const char *nodeCluster;

	/*
	 * Whether or not th enode is active (false in
	 * the case it's in the middle of an addnode)
	 */
	bool isactive;

	/*
	 * The formatted "Mongo compatible" node name
	 * uses node_<clusterName>_<nodeId>
	 */
	const char *mongoNodeName;

	/*
	 * The logical shard for the node in Mongo output
	 * Uses "shard_<groupId>"
	 */
	const char *mongoShardName;
} NodeInfo;


static const char * ColocateUnshardedCitusTables(const char *sourceTableName,
												 const char *colocateWithTableName);
static int GetShardCountForDistributedTable(Oid relationId);

static int GetColocationForTable(Oid tableOid, const char *collectionName,
								 const char *tableName);

static void ColocateShardedCitusTablesWithNone(const char *sourceTableName);
static void ColocateUnshardedCitusTablesWithNone(const char *sourceTableName);
static void MoveShardToDistributedTable(const char *postgresTableToMove, const
										char *targetShardTable);
static void UndistributeAndRedistributeTable(const char *postgresTable, const
											 char *colocateWith,
											 const char *shardKeyValue);

/* Handle a colocation scenario for collMod */
static void HandleDistributedColocation(MongoCollection *collection,
										const bson_value_t *colocationValue);
static Query * RewriteListCollectionsQueryForDistribution(Query *source);
static Query * RewriteConfigShardsQueryForDistribution(Query *source);
static Query * RewriteConfigChunksQueryForDistribution(Query *source);

static List * GetShardMapNodes(void);
static void WriteShardMap(pgbson_writer *writer, List *groupNodes);
static void WriteShardList(pgbson_writer *writer, List *groupNodes);

/*
 * Implements the mongo wire-protocol getShardMap command
 */
Datum
command_get_shard_map(PG_FUNCTION_ARGS)
{
	/* First query pg_dist_node to get the set of nodes in the cluster */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	List *groupNodes = GetShardMapNodes();
	if (groupNodes != NIL)
	{
		WriteShardMap(&writer, groupNodes);
	}


	PgbsonWriterAppendDouble(&writer, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


Datum
command_list_shards(PG_FUNCTION_ARGS)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	List *groupNodes = GetShardMapNodes();
	if (groupNodes != NIL)
	{
		WriteShardList(&writer, groupNodes);
	}


	PgbsonWriterAppendDouble(&writer, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * override hooks related to colocation.
 */
void
UpdateColocationHooks(void)
{
	handle_colocation_hook = HandleDistributedColocation;
	rewrite_list_collections_query_hook = RewriteListCollectionsQueryForDistribution;
	rewrite_config_shards_query_hook = RewriteConfigShardsQueryForDistribution;
	rewrite_config_chunks_query_hook = RewriteConfigChunksQueryForDistribution;
}


/*
 * Process colocation options for a distributed heliodb deployment.
 */
static void
HandleDistributedColocation(MongoCollection *collection, const
							bson_value_t *colocationValue)
{
	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("unexpected - collection for colocation was null")));
	}

	if (!EnableNativeColocation)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
						errmsg("Colocation is not supported yet")));
	}

	if (colocationValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg("colocation options must be a document.")));
	}

	char *tableWithNamespace = psprintf("%s.%s", ApiDataSchemaName,
										collection->tableName);
	bson_iter_t colocationIter;
	BsonValueInitIterator(colocationValue, &colocationIter);

	StringView collectionName = { 0 };
	bool colocateWithNull = false;
	while (bson_iter_next(&colocationIter))
	{
		const char *key = bson_iter_key(&colocationIter);

		if (strcmp(key, "collection") == 0)
		{
			if (BSON_ITER_HOLDS_UTF8(&colocationIter))
			{
				collectionName.string = bson_iter_utf8(&colocationIter,
													   &collectionName.length);
			}
			else if (BSON_ITER_HOLDS_NULL(&colocationIter))
			{
				colocateWithNull = true;
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
								errmsg(
									"colocation.collection must be a string or null. not %s",
									BsonTypeName(bson_iter_type(&colocationIter))),
								errdetail_log(
									"colocation.collection must be a string or null. not %s",
									BsonTypeName(bson_iter_type(&colocationIter)))));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
							errmsg("Unknown field colocation.%s", key),
							errdetail_log("Unknown field colocation.%s", key)));
		}
	}

	if (collectionName.length == 0 && !colocateWithNull)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDOPTIONS),
						errmsg("Must specify collection for colocation")));
	}

	/* For sharded collections, can only colocate with null: We do this to fix up old tables */
	bool isSharded = collection->shardKey != NULL;
	if (isSharded && !colocateWithNull)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDOPTIONS),
						errmsg("Cannot colocate a collection that is already sharded.")));
	}

	const char *retryTableShardKeyValue = NULL;
	if (colocateWithNull)
	{
		if (isSharded)
		{
			retryTableShardKeyValue = "shard_key_value";
			ColocateShardedCitusTablesWithNone(tableWithNamespace);
		}
		else
		{
			ColocateUnshardedCitusTablesWithNone(tableWithNamespace);
		}
	}
	else
	{
		const char *targetCollectionName = CreateStringFromStringView(&collectionName);
		if (strcmp(collection->name.collectionName, targetCollectionName) == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
							errmsg(
								"Source and target cannot be the same for colocation")));
		}

		MongoCollection *targetCollection = GetMongoCollectionByNameDatum(
			CStringGetTextDatum(collection->name.databaseName),
			CStringGetTextDatum(targetCollectionName),
			AccessShareLock);
		if (targetCollection == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
							errmsg("ns %s.%s does not exist",
								   collection->name.databaseName,
								   targetCollectionName),
							errdetail_log("ns %s.%s does not exist",
										  collection->name.databaseName,
										  targetCollectionName)));
		}

		/* Validate that targetCollection is not colocated with mongo_data.changes
		 * (if it is, we fail until it is colocated with null) - this is a back-compat
		 * cleanup decision.
		 */
		char *targetWithNamespace = psprintf("%s.%s", ApiDataSchemaName,
											 targetCollection->tableName);
		int colocationId = GetColocationForTable(targetCollection->relationId,
												 targetCollectionName,
												 targetWithNamespace);

		/* Get the colocationId of the changes table */
		char *mongoDataWithNamespace = psprintf("%s.changes", ApiDataSchemaName);
		RangeVar *rangeVar = makeRangeVar(ApiDataSchemaName, "changes", -1);
		Oid changesRelId = RangeVarGetRelid(rangeVar, AccessShareLock, false);

		int colocationIdOfChangesTable = GetColocationForTable(changesRelId, "changes",
															   mongoDataWithNamespace);
		if (colocationId == colocationIdOfChangesTable)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
							errmsg(
								"Colocation for this collection in the current configuration is not supported. "
								"Please first colocate %s with colocation: null",
								targetCollectionName)),
					(errdetail_log(
						 "Colocation for this table in the current configuration is not supported - legacy table")));
		}

		if (targetCollection->shardKey != NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
							errmsg(
								"Cannot colocate current collection with a sharded collection.")));
		}

		/* Also check if the colocated source has only 1 shard (otherwise require colocate=null explicitly) */
		int shardCount = GetShardCountForDistributedTable(targetCollection->relationId);
		if (shardCount != 1)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
							errmsg(
								"Colocation for this collection in the current configuration is not supported. "
								"Please first colocate %s with colocation: null",
								targetCollectionName)),
					(errdetail_log(
						 "Colocation for this table in the current configuration is not supported - shard count is not 1: %d",
						 shardCount)));
		}

		retryTableShardKeyValue = ColocateUnshardedCitusTables(tableWithNamespace,
															   targetWithNamespace);
	}

	/* Colocate retry with the original table */
	char *retryTableWithNamespace = psprintf("%s.retry_%ld", ApiDataSchemaName,
											 collection->collectionId);
	UndistributeAndRedistributeTable(retryTableWithNamespace, tableWithNamespace,
									 retryTableShardKeyValue);
}


/*
 * Rewrite/Update the metadata query for distributed information.
 * The original listCollections query looks like
 * SELECT bson_dollar_project(row_get_bson(collections), '{ "ns": { "$concat": [ "$database_name", "$collection_name" ], ... }}) FROM mongo_catalog.collections WHERE database_name = 'db';
 *
 * This modifies it to the following:
 * SELECT bson_dollar_addfields(bson_dollar_project(row_get_bson(collections), '{ "ns": { "$concat": [ "$database_name", "$collection_name" ], ... }}),
 *		  bson_repath_and_build('shardcount', pg_dist_colocation.shard_count, 'colocationid', pg_dist_partition.colocationid) FROM mongo_catalog.collections, pg_dist_partition, pg_dist_colocation
 * WHERE database_name = 'db'
 * AND pg_dist_partition.colocationid = pg_dist_colocation.colocationid AND textcatany('ApiDataSchema.documents_', collections.collection_id)::regclass = pg_dist_partition.logicalrelid;
 */
static Query *
RewriteListCollectionsQueryForDistribution(Query *source)
{
	if (list_length(source->rtable) != 1)
	{
		ereport(ERROR, (errmsg("Unexpected error - source query has more than 1 rte")));
	}

	RangeTblEntry *partitionRte = makeNode(RangeTblEntry);

	/* add pg_dist_partition to the RTEs */
	List *partitionColNames = list_concat(list_make3(makeString("logicalrelid"),
													 makeString("partmethod"), makeString(
														 "partkey")),
										  list_make3(makeString("colocationid"),
													 makeString("repmodel"), makeString(
														 "autoconverted")));

	partitionRte->rtekind = RTE_RELATION;
	partitionRte->alias = partitionRte->eref = makeAlias("partition", partitionColNames);
	partitionRte->lateral = false;
	partitionRte->inFromCl = true;
	partitionRte->relkind = RELKIND_RELATION;
	partitionRte->functions = NIL;
	partitionRte->inh = true;
	partitionRte->rellockmode = AccessShareLock;

	RangeVar *rangeVar = makeRangeVar("pg_catalog", "pg_dist_partition", -1);
	partitionRte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);


#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *permInfo = addRTEPermissionInfo(&source->rteperminfos,
													   partitionRte);
	permInfo->requiredPerms = ACL_SELECT;
#else
	partitionRte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = lappend(source->rtable, partitionRte);

	/* Add pg_dist_colocation to the RTEs */
	RangeTblEntry *colocationRte = makeNode(RangeTblEntry);
	List *colocationColNames = list_concat(list_make3(makeString("colocationid"),
													  makeString("shardcount"),
													  makeString("replicationfactor")),
										   list_make2(makeString(
														  "distributioncolumntype"),
													  makeString(
														  "distributioncolumncollation")));

	colocationRte->rtekind = RTE_RELATION;
	colocationRte->alias = colocationRte->eref = makeAlias("colocation",
														   colocationColNames);
	colocationRte->lateral = false;
	colocationRte->inFromCl = true;
	colocationRte->relkind = RELKIND_RELATION;
	colocationRte->functions = NIL;
	colocationRte->inh = true;
	colocationRte->rellockmode = AccessShareLock;

	rangeVar = makeRangeVar("pg_catalog", "pg_dist_colocation", -1);
	colocationRte->relid = RangeVarGetRelid(rangeVar, AccessShareLock, false);

#if PG_VERSION_NUM >= 160000
	permInfo = addRTEPermissionInfo(&source->rteperminfos,
									colocationRte);
	permInfo->requiredPerms = ACL_SELECT;
#else
	colocationRte->requiredPerms = ACL_SELECT;
#endif

	source->rtable = lappend(source->rtable, colocationRte);

	RangeTblRef *secondRef = makeNode(RangeTblRef);
	secondRef->rtindex = 2;
	RangeTblRef *thirdRef = makeNode(RangeTblRef);
	thirdRef->rtindex = 3;
	FromExpr *currentTree = source->jointree;
	currentTree->fromlist = lappend(currentTree->fromlist, secondRef);
	currentTree->fromlist = lappend(currentTree->fromlist, thirdRef);

	/* now add the "join" to the quals */
	List *existingQuals = make_ands_implicit((Expr *) currentTree->quals);

	/* On the collections_table we take the collection_id */
	Var *collectionIdVar = makeVar(1, 3, INT8OID, -1, InvalidOid, 0);

	char *tablePrefixString = psprintf("%s.%s", ApiDataSchemaName,
									   MONGO_DATA_TABLE_NAME_PREFIX);
	Const *tablePrefix = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(
									   tablePrefixString), false, false);

	/* Construct the string */
	FuncExpr *concatExpr = makeFuncExpr(F_TEXTANYCAT, TEXTOID,
										list_make2(tablePrefix, collectionIdVar),
										InvalidOid, DEFAULT_COLLATION_OID,
										COERCE_EXPLICIT_CALL);

	FuncExpr *castConcatExpr = makeFuncExpr(F_REGCLASS, OIDOID, list_make1(concatExpr),
											DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
											COERCE_EXPLICIT_CALL);

	/* Get the regclass of the join */
	Var *regclassVar = makeVar(2, 1, OIDOID, -1, InvalidOid, 0);
	FuncExpr *oidEqualFunc = makeFuncExpr(F_OIDEQ, BOOLOID, list_make2(regclassVar,
																	   castConcatExpr),
										  DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
										  COERCE_EXPLICIT_CALL);
	existingQuals = lappend(existingQuals, oidEqualFunc);

	List *secondJoinArgs = list_make2(
		makeVar(2, 4, INT4OID, -1, InvalidOid, 0),
		makeVar(3, 1, INT4OID, -1, InvalidOid, 0)
		);
	FuncExpr *secondJoin = makeFuncExpr(F_OIDEQ, BOOLOID, secondJoinArgs, InvalidOid,
										InvalidOid, COERCE_EXPLICIT_CALL);
	existingQuals = lappend(existingQuals, secondJoin);
	currentTree->quals = (Node *) make_ands_explicit(existingQuals);

	List *repathArgs = list_make4(
		makeConst(TEXTOID, -1, DEFAULT_COLLATION_OID, -1, CStringGetTextDatum(
					  "colocationId"), false, false),
		makeVar(2, 4, INT4OID, -1, InvalidOid, 0),
		makeConst(TEXTOID, -1, DEFAULT_COLLATION_OID, -1, CStringGetTextDatum(
					  "shardCount"), false, false),
		makeVar(3, 2, INT4OID, -1, InvalidOid, 0));
	FuncExpr *colocationArgs = makeFuncExpr(BsonRepathAndBuildFunctionOid(), BsonTypeId(),
											repathArgs, InvalidOid, InvalidOid,
											COERCE_EXPLICIT_CALL);


	Oid addFieldsOid = IsClusterVersionAtleastThis(1, 18, 0) ?
					   BsonDollaMergeDocumentsFunctionOid() :
					   BsonDollarAddFieldsFunctionOid();

	TargetEntry *firstEntry = linitial(source->targetList);
	FuncExpr *addFields = makeFuncExpr(addFieldsOid, BsonTypeId(),
									   list_make2(firstEntry->expr, colocationArgs),
									   InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	firstEntry->expr = (Expr *) addFields;

	return source;
}


/*
 * The config.shards query in a distributed query scenario
 * will end up querying the pg_dist_node table to get the list of shards
 * and output them in a mongo compatible format.
 */
static Query *
RewriteConfigShardsQueryForDistribution(Query *baseQuery)
{
	Query *query = makeNode(Query);
	query->commandType = CMD_SELECT;
	query->querySource = QSRC_ORIGINAL;
	query->canSetTag = true;

	List *valuesList = NIL;

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	List *groupNodes = GetShardMapNodes();
	if (groupNodes != NIL)
	{
		WriteShardList(&writer, groupNodes);
	}

	pgbson *shardList = PgbsonWriterGetPgbson(&writer);

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(shardList, &element);

	bson_iter_t values;
	BsonValueInitIterator(&element.bsonValue, &values);

	while (bson_iter_next(&values))
	{
		pgbson *bsonValue = PgbsonInitFromDocumentBsonValue(bson_iter_value(&values));
		valuesList = lappend(valuesList, list_make1(makeConst(BsonTypeId(), -1,
															  InvalidOid, -1,
															  PointerGetDatum(bsonValue),
															  false, false)));
	}

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

	return query;
}


static Oid
GetCitusShardSizesFunctionOid(void)
{
	List *functionNameList = list_make2(makeString("pg_catalog"),
										makeString("citus_shard_sizes"));
	bool missingOK = false;

	return LookupFuncName(functionNameList, 0, NULL, missingOK);
}


/*
 * Provides the output for the config.chunks query after consulting with Citus.
 * This will be the query:
 * WITH coll AS (SELECT database_name, collection_name, ('mongo_data.documents_' || collection_id)::regclass AS tableId FROM mongo_catalog.collections)
 * SELECT database_name, collection_name, shardid, size, shardminvalue, shardmaxvalue FROM coll
 *  JOIN pg_dist_shard dist ON coll.tableId = dist.logicalrelid
 *  JOIN citus_shard_sizes() sz ON dist.shardid = sz.shard_id;
 */
static Query *
RewriteConfigChunksQueryForDistribution(Query *baseQuery)
{
	Query *source = makeNode(Query);
	source->commandType = CMD_SELECT;
	source->querySource = QSRC_ORIGINAL;
	source->canSetTag = true;

	/* Match spec for ApiCatalogSchemaName.collections function */
	RangeTblEntry *rte = makeNode(RangeTblEntry);
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
	RTEPermissionInfo *sourcePermInfo = addRTEPermissionInfo(&source->rteperminfos, rte);
	sourcePermInfo->requiredPerms = ACL_SELECT;
#else
	rte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = list_make1(rte);

	RangeTblEntry *shardsRte = makeNode(RangeTblEntry);

	/* add pg_dist_shard to the RTEs */
	List *shardColNames = list_make5(makeString("logicalrelid"), makeString("shardid"),
									 makeString("shardstorage"),
									 makeString("shardminvalue"), makeString(
										 "shardmaxvalue"));

	shardsRte->rtekind = RTE_RELATION;
	shardsRte->alias = shardsRte->eref = makeAlias("shards", shardColNames);
	shardsRte->lateral = false;
	shardsRte->inFromCl = true;
	shardsRte->relkind = RELKIND_RELATION;
	shardsRte->functions = NIL;
	shardsRte->inh = true;
	shardsRte->rellockmode = AccessShareLock;

	RangeVar *shardRangeVar = makeRangeVar("pg_catalog", "pg_dist_shard", -1);
	shardsRte->relid = RangeVarGetRelid(shardRangeVar, AccessShareLock, false);


#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *shardsPermInfo = addRTEPermissionInfo(&source->rteperminfos,
															 shardsRte);
	shardsPermInfo->requiredPerms = ACL_SELECT;
#else
	shardsRte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = lappend(source->rtable, shardsRte);

	/* Add citus_shard_sizes() to the RTEs */
	RangeTblEntry *shardSizeRte = makeNode(RangeTblEntry);
	List *shardSizesColNames = list_make2(makeString("shard_id"),
										  makeString("size"));

	shardSizeRte->rtekind = RTE_FUNCTION;
	shardSizeRte->alias = shardSizeRte->eref = makeAlias("shard_sizes",
														 shardSizesColNames);
	shardSizeRte->lateral = false;
	shardSizeRte->inFromCl = true;
	shardSizeRte->functions = NIL;
	shardSizeRte->inh = false;
	shardSizeRte->rellockmode = AccessShareLock;

	Oid citusShardsSizesFuncId = GetCitusShardSizesFunctionOid();
	FuncExpr *shardSizesFunc = makeFuncExpr(citusShardsSizesFuncId, RECORDOID, NIL,
											InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);

	RangeTblFunction *shardSizesFunctions = makeNode(RangeTblFunction);
	shardSizesFunctions->funcexpr = (Node *) shardSizesFunc;
	shardSizesFunctions->funccolcount = 2;
	shardSizesFunctions->funccoltypes = list_make2_oid(INT4OID, INT8OID);
	shardSizesFunctions->funccolcollations = list_make2_oid(InvalidOid, InvalidOid);
	shardSizesFunctions->funccoltypmods = list_make2_int(-1, -1);

	shardSizeRte->functions = list_make1(shardSizesFunctions);

#if PG_VERSION_NUM >= 160000
	shardSizeRte->perminfoindex = 0;
#else
	shardSizeRte->requiredPerms = ACL_SELECT;
#endif

	source->rtable = lappend(source->rtable, shardSizeRte);

	/* Add pg_dist_placement */
	RangeTblEntry *placementRte = makeNode(RangeTblEntry);
	List *placementColNames = list_make5(makeString("placementid"), makeString("shardid"),
										 makeString("shardstate"),
										 makeString("shardlength"), makeString(
											 "groupid"));

	placementRte->rtekind = RTE_RELATION;
	placementRte->alias = placementRte->eref = makeAlias("placement", placementColNames);
	placementRte->lateral = false;
	placementRte->inFromCl = true;
	placementRte->relkind = RELKIND_RELATION;
	placementRte->functions = NIL;
	placementRte->inh = true;
	placementRte->rellockmode = AccessShareLock;

	RangeVar *placementRangeVar = makeRangeVar("pg_catalog", "pg_dist_placement", -1);
	placementRte->relid = RangeVarGetRelid(placementRangeVar, AccessShareLock, false);


#if PG_VERSION_NUM >= 160000
	RTEPermissionInfo *placementPermInfo = addRTEPermissionInfo(&source->rteperminfos,
																placementRte);
	placementPermInfo->requiredPerms = ACL_SELECT;
#else
	placementRte->requiredPerms = ACL_SELECT;
#endif
	source->rtable = lappend(source->rtable, placementRte);


	RangeTblRef *collectionsRef = makeNode(RangeTblRef);
	collectionsRef->rtindex = 1;
	RangeTblRef *shardsRef = makeNode(RangeTblRef);
	shardsRef->rtindex = 2;
	RangeTblRef *shardSizesRef = makeNode(RangeTblRef);
	shardSizesRef->rtindex = 3;
	RangeTblRef *placementRef = makeNode(RangeTblRef);
	placementRef->rtindex = 4;

	List *quals = NIL;

	/* WHERE view_definition IS NOT NULL */
	NullTest *nullTest = makeNode(NullTest);
	nullTest->argisrow = false;
	nullTest->nulltesttype = IS_NULL;
	nullTest->arg = (Expr *) makeVar(collectionsRef->rtindex, 6, BsonTypeId(), -1,
									 InvalidOid, 0);
	quals = lappend(quals, nullTest);

	/* Join collection with shard */
	Var *collectionIdVar = makeVar(collectionsRef->rtindex, 3, INT8OID, -1, InvalidOid,
								   0);

	char *tablePrefixString = psprintf("%s.%s", ApiDataSchemaName,
									   MONGO_DATA_TABLE_NAME_PREFIX);
	Const *tablePrefix = makeConst(TEXTOID, -1, InvalidOid, -1, CStringGetTextDatum(
									   tablePrefixString), false, false);

	/* Construct the string */
	FuncExpr *concatExpr = makeFuncExpr(F_TEXTANYCAT, TEXTOID,
										list_make2(tablePrefix, collectionIdVar),
										InvalidOid, DEFAULT_COLLATION_OID,
										COERCE_EXPLICIT_CALL);

	FuncExpr *castConcatExpr = makeFuncExpr(F_REGCLASS, OIDOID, list_make1(concatExpr),
											DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
											COERCE_EXPLICIT_CALL);

	/* Get the regclass of the join */
	Var *regclassVar = makeVar(shardsRef->rtindex, 1, OIDOID, -1, InvalidOid, 0);
	FuncExpr *oidEqualFunc = makeFuncExpr(F_OIDEQ, BOOLOID, list_make2(regclassVar,
																	   castConcatExpr),
										  DEFAULT_COLLATION_OID, DEFAULT_COLLATION_OID,
										  COERCE_EXPLICIT_CALL);
	quals = lappend(quals, oidEqualFunc);

	/* Join pg_dist_shard with shard_sizes */
	Var *shardIdLeftVar = makeVar(shardsRef->rtindex, 2, INT8OID, -1, -1, 0);
	Var *shardIdRightVar = makeVar(shardSizesRef->rtindex, 1, INT8OID, -1, -1, 0);
	FuncExpr *shardIdEqual = makeFuncExpr(F_INT8EQ, BOOLOID, list_make2(shardIdLeftVar,
																		shardIdRightVar),
										  InvalidOid, InvalidOid, COERCE_EXPLICIT_CALL);
	quals = lappend(quals, shardIdEqual);

	/* Join pg_dist_shard with pg_dist_placement */
	Var *shardIdPlacementVar = makeVar(placementRef->rtindex, 2, INT8OID, -1, -1, 0);
	FuncExpr *shardIdPlacementEqual = makeFuncExpr(F_INT8EQ, BOOLOID, list_make2(
													   shardIdLeftVar,
													   shardIdPlacementVar),
												   InvalidOid, InvalidOid,
												   COERCE_EXPLICIT_CALL);
	quals = lappend(quals, shardIdPlacementEqual);

	source->jointree = makeFromExpr(list_make4(collectionsRef, shardsRef, shardSizesRef,
											   placementRef), (Node *) make_ands_explicit(
										quals));

	Const *groupPrefix = MakeTextConst("shard_", 6);
	FuncExpr *groupIdStr = makeFuncExpr(F_TEXTANYCAT, TEXTOID,
										list_make2(groupPrefix, makeVar(
													   placementRef->rtindex, 5, INT4OID,
													   -1, -1, 0)),
										InvalidOid, DEFAULT_COLLATION_OID,
										COERCE_EXPLICIT_CALL);
	source->targetList = list_make5(
		makeTargetEntry((Expr *) makeVar(collectionsRef->rtindex, 1, TEXTOID, -1, -1, 0),
						1, "database_name", false),
		makeTargetEntry((Expr *) makeVar(collectionsRef->rtindex, 2, TEXTOID, -1, -1, 0),
						2, "collection_name", false),
		makeTargetEntry((Expr *) makeVar(shardsRef->rtindex, 2, INT8OID, -1, -1, 0), 3,
						"shard_id", false),
		makeTargetEntry((Expr *) makeVar(shardSizesRef->rtindex, 2, INT8OID, -1, -1, 0),
						4, "size", false),
		makeTargetEntry((Expr *) groupIdStr, 5, "groupid", false));
	source->targetList = lappend(source->targetList, makeTargetEntry((Expr *) makeVar(
																		 shardsRef->
																		 rtindex, 5,
																		 TEXTOID, -1, -1,
																		 0), 6,
																	 "shard_min", false));
	source->targetList = lappend(source->targetList, makeTargetEntry((Expr *) makeVar(
																		 shardsRef->
																		 rtindex, 6,
																		 TEXTOID, -1, -1,
																		 0), 7,
																	 "shard_max", false));

	RangeTblEntry *subQueryRte = MakeSubQueryRte(source, 1, 0, "config_shards_base",
												 true);

	Var *rowExpr = makeVar(1, 0, RECORDOID, -1, InvalidOid, 0);
	FuncExpr *funcExpr = makeFuncExpr(RowGetBsonFunctionOid(), BsonTypeId(),
									  list_make1(rowExpr), InvalidOid, InvalidOid,
									  COERCE_EXPLICIT_CALL);

	/* Build the projection spec for this query */
	pgbson_writer specWriter;
	PgbsonWriterInit(&specWriter);

	PgbsonWriterAppendUtf8(&specWriter, "_id", 3, "$shard_id");

	/* Write ns: <db.coll> */
	pgbson_writer childWriter;
	PgbsonWriterStartDocument(&specWriter, "ns", 2, &childWriter);

	pgbson_array_writer childArray;
	PgbsonWriterStartArray(&childWriter, "$concat", 7, &childArray);
	PgbsonArrayWriterWriteUtf8(&childArray, "$database_name");
	PgbsonArrayWriterWriteUtf8(&childArray, ".");
	PgbsonArrayWriterWriteUtf8(&childArray, "$collection_name");
	PgbsonWriterEndArray(&childWriter, &childArray);
	PgbsonWriterEndDocument(&specWriter, &childWriter);

	PgbsonWriterAppendUtf8(&specWriter, "min", 3, "$shard_min");
	PgbsonWriterAppendUtf8(&specWriter, "max", 3, "$shard_max");
	PgbsonWriterAppendUtf8(&specWriter, "chunkSize", 9, "$size");
	PgbsonWriterAppendUtf8(&specWriter, "shard", 5, "$groupid");

	Const *specConst = MakeBsonConst(PgbsonWriterGetPgbson(&specWriter));
	funcExpr = makeFuncExpr(BsonDollarProjectFunctionOid(), BsonTypeId(),
							list_make2(funcExpr, specConst), InvalidOid, InvalidOid,
							COERCE_EXPLICIT_CALL);

	TargetEntry *upperEntry = makeTargetEntry((Expr *) funcExpr, 1, "document",
											  false);
	Query *newquery = makeNode(Query);
	newquery->commandType = CMD_SELECT;
	newquery->querySource = source->querySource;
	newquery->canSetTag = true;
	newquery->targetList = list_make1(upperEntry);
	newquery->rtable = list_make1(subQueryRte);

	RangeTblRef *rtr = makeNode(RangeTblRef);
	rtr->rtindex = 1;
	newquery->jointree = makeFromExpr(list_make1(rtr), NULL);

	return newquery;
}


static void
UndistributeAndRedistributeTable(const char *postgresTable, const char *colocateWith,
								 const char *shardKeyValue)
{
	bool readOnly = false;
	bool resultNullIgnore = false;
	Oid tableDetailsArgTypes[3] = { TEXTOID, TEXTOID, TEXTOID };
	Datum tableDetailsArgValues[3] = {
		CStringGetTextDatum(postgresTable), CStringGetTextDatum(colocateWith), (Datum) 0
	};
	char argNulls[3] = { ' ', ' ', 'n' };

	/* This is a distributed table with distributionColumn == shard_key_value */
	const char *undistributeTable = "SELECT undistribute_table($1)";

	/* First undistribute the table.*/
	ExtensionExecuteQueryWithArgsViaSPI(undistributeTable, 1, tableDetailsArgTypes,
										tableDetailsArgValues, argNulls, readOnly,
										SPI_OK_SELECT, &resultNullIgnore);

	/* Then redistribute it as a single shard distributed table */
	if (shardKeyValue != NULL)
	{
		tableDetailsArgValues[2] = CStringGetTextDatum(shardKeyValue);
		argNulls[2] = ' ';
	}

	const char *redistributeTable =
		"SELECT create_distributed_table($1::regclass, distribution_column => $3, colocate_with => $2)";
	ExtensionExecuteQueryWithArgsViaSPI(redistributeTable, 3, tableDetailsArgTypes,
										tableDetailsArgValues, argNulls, readOnly,
										SPI_OK_SELECT, &resultNullIgnore);
}


/*
 * For sharded citus tables, colocates the table with "none".
 */
static void
ColocateShardedCitusTablesWithNone(const char *sourceTableName)
{
	const char *colocateQuery =
		"SELECT alter_distributed_table(table_name => $1, colocate_with => $2, cascade_to_colocated => false)";

	Oid argTypes[2] = { TEXTOID, TEXTOID };
	Datum argValues[2] = {
		CStringGetTextDatum(sourceTableName), CStringGetTextDatum("none")
	};
	char *argNulls = NULL;

	bool isNull = false;
	int colocateArgs = 2;
	ExtensionExecuteQueryWithArgsViaSPI(colocateQuery, colocateArgs, argTypes, argValues,
										argNulls, false, SPI_OK_SELECT, &isNull);
}


/*
 * Gets the distribution details of a given citus table.
 */
static void
GetCitusTableDistributionDetails(const char *sourceTableName, const char **citusTableType,
								 const char **distributionColumn, int64 *shardCount)
{
	const char *tableDetailsQuery =
		"SELECT citus_table_type, distribution_column, shard_count FROM public.citus_tables WHERE table_name = $1::regclass";

	Oid tableDetailsArgTypes[1] = { TEXTOID };
	Datum tableDetailsArgValues[1] = { CStringGetTextDatum(sourceTableName) };
	char *argNulls = NULL;
	bool readOnly = true;

	Datum results[3] = { 0 };
	bool resultNulls[3] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		tableDetailsQuery, 1, tableDetailsArgTypes, tableDetailsArgValues, argNulls,
		readOnly, SPI_OK_SELECT, results, resultNulls, 3);

	if (resultNulls[0] || resultNulls[1] || resultNulls[2])
	{
		ereport(ERROR, (errmsg(
							"Unexpected result found null value for shards query [0]=%d, [1]=%d, [2]=%d",
							resultNulls[0], resultNulls[1], resultNulls[2])));
	}

	*citusTableType = TextDatumGetCString(results[0]);
	*distributionColumn = TextDatumGetCString(results[1]);
	*shardCount = DatumGetInt64(results[2]);
}


/*
 * breaks colocation for unsharded citus tables.
 */
static void
ColocateUnshardedCitusTablesWithNone(const char *sourceTableName)
{
	/* First get the current distribution mode/column. */
	const char *citusTableType = NULL;
	const char *distributionColumn = NULL;
	int64 shardCount = 0;
	GetCitusTableDistributionDetails(sourceTableName, &citusTableType,
									 &distributionColumn, &shardCount);

	ereport(NOTICE, (errmsg(
						 "Current table type %s, distribution column %s, shardCount %ld",
						 citusTableType,
						 distributionColumn, shardCount)));

	/* Scenario 1: It's already a single shard distributed table */
	bool readOnly = false;
	char *argNulls = NULL;
	bool resultNullIgnore = false;
	Oid tableDetailsArgTypes[1] = { TEXTOID };
	Datum tableDetailsArgValues[1] = { CStringGetTextDatum(sourceTableName) };
	if (strcmp(distributionColumn, "<none>") == 0)
	{
		const char *updateColocationQuery =
			"SELECT update_distributed_table_colocation($1, colocate_with => 'none')";
		ExtensionExecuteQueryWithArgsViaSPI(updateColocationQuery, 1,
											tableDetailsArgTypes, tableDetailsArgValues,
											argNulls, readOnly, SPI_OK_SELECT,
											&resultNullIgnore);
	}
	else
	{
		const char *colocateWith = "none";
		distributionColumn = NULL;
		UndistributeAndRedistributeTable(sourceTableName, colocateWith,
										 distributionColumn);
	}
}


/*
 * Core logic for colocating 2 unsharded citus tables.
 */
static const char *
ColocateUnshardedCitusTables(const char *tableToColocate, const
							 char *colocateWithTableName)
{
	const char *sourceCitusTableType = NULL;
	const char *sourceDistributionColumn = NULL;
	int64 sourceShardCount = 0;
	GetCitusTableDistributionDetails(tableToColocate, &sourceCitusTableType,
									 &sourceDistributionColumn, &sourceShardCount);

	ereport(INFO, (errmsg("Source table type %s, distribution column %s, shardCount %ld",
						  sourceCitusTableType, sourceDistributionColumn,
						  sourceShardCount)));
	if (sourceShardCount != 1)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
						errmsg(
							"Cannot colocate collection to source in current state. Please colocate the source collection with colocation: none")));
	}

	bool resultNullIgnore = false;
	char *argNulls = NULL;
	const char *targetCitusTableType = NULL;
	const char *targetDistributionColumn = NULL;
	int64 targetShardCount = 0;
	GetCitusTableDistributionDetails(colocateWithTableName, &targetCitusTableType,
									 &targetDistributionColumn, &targetShardCount);

	ereport(INFO, (errmsg("Target table type %s, distribution column %s, shardCount %ld",
						  targetCitusTableType, targetDistributionColumn,
						  targetShardCount)));
	bool isTableSingleShardDistributed = strcmp(tableToColocate, "<none>") == 0;
	if (strcmp(targetDistributionColumn, "<none>") == 0)
	{
		/* target is a single shard distributed table */
		if (isTableSingleShardDistributed)
		{
			Oid tableDetailsArgTypes[2] = { TEXTOID, TEXTOID };
			Datum tableDetailsArgValues[2] = {
				CStringGetTextDatum(tableToColocate), (Datum) 0
			};

			/* First update colocation data to none */
			const char *updateDistributionToNoneQuery =
				"SELECT update_distributed_table_colocation($1, colocate_with => 'none')";

			bool readOnly = false;
			ExtensionExecuteQueryWithArgsViaSPI(updateDistributionToNoneQuery, 1,
												tableDetailsArgTypes,
												tableDetailsArgValues, argNulls, readOnly,
												SPI_OK_SELECT, &resultNullIgnore);

			/* Then move the shard to match the source table */
			MoveShardToDistributedTable(tableToColocate, colocateWithTableName);


			/* Match the colocation to the other table */
			const char *updateDistributionToTargetTable =
				"SELECT update_distributed_table_colocation($1, colocate_with => $2)";
			tableDetailsArgValues[1] = CStringGetTextDatum(colocateWithTableName);

			ExtensionExecuteQueryWithArgsViaSPI(updateDistributionToTargetTable, 2,
												tableDetailsArgTypes,
												tableDetailsArgValues, argNulls, readOnly,
												SPI_OK_SELECT, &resultNullIgnore);
		}
		else
		{
			/* This is a distributed table with distributionColumn == shard_key_value */
			const char *distributionColumn = NULL;
			UndistributeAndRedistributeTable(tableToColocate, colocateWithTableName,
											 distributionColumn);
		}

		/* Distribution column is none */
		return NULL;
	}
	else
	{
		/* The target is a distributed table with a single shard (legacy tables) */
		if (isTableSingleShardDistributed)
		{
			/* We need to back-convert this to a single shard distributed table. */
			/* This is a distributed table with distributionColumn == shard_key_value */
			UndistributeAndRedistributeTable(tableToColocate, colocateWithTableName,
											 "shard_key_value");
		}
		else
		{
			/* Neither tables are single shard distributed. */
			/* We need to back-convert this to a single shard distributed table. */
			/* This is a distributed table with distributionColumn == shard_key_value */
			Oid tableDetailsArgTypes[2] = { TEXTOID, TEXTOID };
			Datum tableDetailsArgValues[2] = {
				CStringGetTextDatum(tableToColocate), CStringGetTextDatum(
					colocateWithTableName)
			};

			/* Now create distributed table with colocation with the target */
			const char *alterDistributedTableQuery =
				"SELECT alter_distributed_table(table_name => $1, colocate_with => $2)";

			bool readOnly = false;
			ExtensionExecuteQueryWithArgsViaSPI(alterDistributedTableQuery, 2,
												tableDetailsArgTypes,
												tableDetailsArgValues, argNulls, readOnly,
												SPI_OK_SELECT, &resultNullIgnore);
		}

		/* legacy table value */
		return "shard_key_value";
	}
}


static int
GetShardCountForDistributedTable(Oid relationId)
{
	const char *shardIdCountQuery =
		"SELECT COUNT(*) FROM pg_dist_shard WHERE logicalrelid = $1";

	Oid shardCountArgTypes[1] = { OIDOID };
	Datum shardCountArgValues[1] = { ObjectIdGetDatum(relationId) };
	char *argNullNone = NULL;
	bool readOnly = true;
	bool isNull = false;
	Datum shardIdDatum = ExtensionExecuteQueryWithArgsViaSPI(shardIdCountQuery, 1,
															 shardCountArgTypes,
															 shardCountArgValues,
															 argNullNone, readOnly,
															 SPI_OK_SELECT,
															 &isNull);
	if (isNull)
	{
		return 0;
	}

	return DatumGetInt64(shardIdDatum);
}


/*
 * Gets the colocationId for a given pg table.
 */
static int
GetColocationForTable(Oid tableOid, const char *collectionName, const char *tableName)
{
	char *colocationId =
		"SELECT colocationid FROM pg_dist_partition WHERE logicalrelid = $1";

	int numArgs = 1;
	Oid argTypes[1] = { OIDOID };
	Datum argValues[1] = { ObjectIdGetDatum(tableOid) };
	char *argNulls = NULL;

	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(colocationId, numArgs, argTypes,
													   argValues,
													   argNulls, false, SPI_OK_SELECT,
													   &isNull);
	if (isNull)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg(
							"Could not find collection in internal colocation metadata: %s",
							collectionName),
						errdetail_log(
							"Could not find collection in internal colocation metadata %s: %s",
							collectionName, tableName)));
	}

	return DatumGetInt32(result);
}


/*
 * Gets the node level details for a single shard table.
 */
static void
GetNodeNamePortForPostgresTable(const char *postgresTable, char **nodeName, int *nodePort,
								int64 *shardId)
{
	char *allArgsProvidedNulls = NULL;
	int shardIdQuerynArgs = 1;
	Datum shardIdArgValues[1] = { CStringGetTextDatum(postgresTable) };
	Oid shardIdArgTypes[1] = { TEXTOID };
	bool readOnly = false;
	bool isNull = false;

	const char *shardIdQuery =
		"SELECT shardid FROM pg_dist_shard WHERE logicalrelid = $1::regclass";

	/* While all these queries appear read-only inner Citus code seems to break this assumption
	 * And these queries fail when readOnly is marked as TRUE. Until the underlying issue is fixed
	 * in citus, these are left as read-only false.
	 */
	Datum shardIdValue = ExtensionExecuteQueryWithArgsViaSPI(shardIdQuery,
															 shardIdQuerynArgs,
															 shardIdArgTypes,
															 shardIdArgValues,
															 allArgsProvidedNulls,
															 readOnly,
															 SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg("Could not extract shard_id for newly created table"),
						errdetail_log("Could not get shardId value for postgres table %s",
									  postgresTable)));
	}

	/* Now get the source node/port for this shard */
	const char *shardPlacementTable =
		"SELECT nodename, nodeport FROM pg_dist_shard_placement WHERE shardid = $1";

	int shardPlacementArgs = 1;
	Datum shardPlacementArgValues[1] = { shardIdValue };
	Oid shardPlacementArgTypes[1] = { INT8OID };

	Datum currentNodeDatums[2] = { 0 };
	bool currentNodeIsNulls[2] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(shardPlacementTable, shardPlacementArgs,
												  shardPlacementArgTypes,
												  shardPlacementArgValues,
												  allArgsProvidedNulls, readOnly,
												  SPI_OK_SELECT, currentNodeDatums,
												  currentNodeIsNulls, 2);

	if (currentNodeIsNulls[0] || currentNodeIsNulls[1])
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg(
							"Could not find shard placement for newly created table shard"),
						errdetail_log(
							"Could not find shardId %ld in the placement table for table %s: node is null %d, port is null %d",
							DatumGetInt64(shardIdValue), postgresTable,
							currentNodeIsNulls[0], currentNodeIsNulls[1])));
	}

	*nodeName = TextDatumGetCString(currentNodeDatums[0]);
	*nodePort = DatumGetInt32(currentNodeDatums[1]);
	*shardId = DatumGetInt64(shardIdValue);
}


/*
 * Moves a single shard table to be colocated with another target single shard table.
 */
static void
MoveShardToDistributedTable(const char *postgresTableToMove, const char *targetShardTable)
{
	char *toMoveNodeName, *targetNodeName;
	int toMoveNodePort, targetNodePort;
	int64 toMoveShardId, targetShardId;
	GetNodeNamePortForPostgresTable(postgresTableToMove, &toMoveNodeName, &toMoveNodePort,
									&toMoveShardId);
	GetNodeNamePortForPostgresTable(targetShardTable, &targetNodeName, &targetNodePort,
									&targetShardId);

	elog(INFO, "Moving shard %ld from %s:%d to %s:%d", DatumGetInt64(toMoveShardId),
		 toMoveNodeName, toMoveNodePort,
		 targetNodeName, targetNodePort);
	const char *moveShardQuery =
		"SELECT citus_move_shard_placement(shard_id => $1, source_node_name => $2, source_node_port => $3,"
		" target_node_name => $4, target_node_port => $5, shard_transfer_mode => 'block_writes'::citus.shard_transfer_mode)";
	Datum moveShardDatums[5] =
	{
		toMoveShardId,
		CStringGetTextDatum(toMoveNodeName),
		Int32GetDatum(toMoveNodePort),
		CStringGetTextDatum(targetNodeName),
		Int32GetDatum(targetNodePort)
	};

	Oid moveShardTypes[5] =
	{
		INT8OID,
		TEXTOID,
		INT4OID,
		TEXTOID,
		INT4OID
	};

	bool resultIsNullIgnore = false;
	char *allArgsProvidedNulls = NULL;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(moveShardQuery, 5, moveShardTypes,
										moveShardDatums, allArgsProvidedNulls,
										readOnly, SPI_OK_SELECT, &resultIsNullIgnore);
}


static void
WriteShardMap(pgbson_writer *writer, List *groupMap)
{
	/* Now that we have the list ordered by group & role, write out the object */
	/* First object is "map" */
	pgbson_writer childWriter;
	StringInfo hostStringInfo = makeStringInfo();
	int32_t groupId = -1;
	ListCell *listCell;
	const char *shardName = NULL;
	char *separator = "";
	PgbsonWriterStartDocument(writer, "map", 3, &childWriter);

	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		if (nodeInfo->groupId != groupId)
		{
			/* New groupId */
			if (shardName != NULL)
			{
				PgbsonWriterAppendUtf8(&childWriter, shardName, -1, hostStringInfo->data);
				resetStringInfo(hostStringInfo);
			}

			shardName = nodeInfo->mongoShardName;
			groupId = nodeInfo->groupId;
			appendStringInfo(hostStringInfo, "%s/", nodeInfo->mongoShardName);
			separator = "";
		}

		if (nodeInfo->isactive)
		{
			appendStringInfo(hostStringInfo, "%s%s", separator, nodeInfo->mongoNodeName);
			separator = ",";
		}
	}

	if (shardName != NULL && hostStringInfo->len > 0)
	{
		PgbsonWriterAppendUtf8(&childWriter, shardName, -1, hostStringInfo->data);
	}

	PgbsonWriterEndDocument(writer, &childWriter);

	/* Now write the hosts object */
	PgbsonWriterStartDocument(writer, "hosts", 5, &childWriter);

	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		if (nodeInfo->isactive)
		{
			PgbsonWriterAppendUtf8(&childWriter, nodeInfo->mongoNodeName, -1,
								   nodeInfo->mongoShardName);
		}
	}

	PgbsonWriterEndDocument(writer, &childWriter);

	/* Now write the nodes object */
	PgbsonWriterStartDocument(writer, "nodes", 5, &childWriter);

	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		pgbson_writer nodeWriter;
		PgbsonWriterStartDocument(&childWriter, nodeInfo->mongoNodeName, -1, &nodeWriter);
		PgbsonWriterAppendUtf8(&nodeWriter, "role", 4, nodeInfo->nodeRole);
		PgbsonWriterAppendBool(&nodeWriter, "active", 6, nodeInfo->isactive);
		PgbsonWriterAppendUtf8(&nodeWriter, "cluster", 7, nodeInfo->nodeCluster);
		PgbsonWriterEndDocument(&childWriter, &nodeWriter);
	}

	PgbsonWriterEndDocument(writer, &childWriter);
}


static void
WriteShardList(pgbson_writer *writer, List *groupMap)
{
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(writer, "shards", 6, &arrayWriter);

	ListCell *listCell;
	StringInfo hostStringInfo = makeStringInfo();
	int32_t groupId = -1;
	const char *shardName = NULL;
	char *separator = "";
	pgbson_writer nestedObjectWriter;
	foreach(listCell, groupMap)
	{
		NodeInfo *nodeInfo = lfirst(listCell);
		if (nodeInfo->groupId != groupId)
		{
			/* New groupId */
			if (shardName != NULL)
			{
				PgbsonArrayWriterStartDocument(&arrayWriter, &nestedObjectWriter);
				PgbsonWriterAppendUtf8(&nestedObjectWriter, "_id", 3, shardName);
				PgbsonWriterAppendUtf8(&nestedObjectWriter, "nodes", 5,
									   hostStringInfo->data);
				PgbsonArrayWriterEndDocument(&arrayWriter, &nestedObjectWriter);
				resetStringInfo(hostStringInfo);
			}

			shardName = nodeInfo->mongoShardName;
			groupId = nodeInfo->groupId;
			appendStringInfo(hostStringInfo, "%s/", nodeInfo->mongoShardName);
			separator = "";
		}

		if (nodeInfo->isactive)
		{
			appendStringInfo(hostStringInfo, "%s%s", separator, nodeInfo->mongoNodeName);
			separator = ",";
		}
	}

	if (shardName != NULL && hostStringInfo->len > 0)
	{
		PgbsonArrayWriterStartDocument(&arrayWriter, &nestedObjectWriter);
		PgbsonWriterAppendUtf8(&nestedObjectWriter, "_id", 3, shardName);
		PgbsonWriterAppendUtf8(&nestedObjectWriter, "nodes", 5, hostStringInfo->data);
		PgbsonArrayWriterEndDocument(&arrayWriter, &nestedObjectWriter);
	}

	PgbsonWriterEndArray(writer, &arrayWriter);
}


/*
 * Fetches the nodes in the cluster ordered by groupId and nodeRole.
 */
static List *
GetShardMapNodes(void)
{
	/* First query pg_dist_node to get the set of nodes in the cluster */
	const char *baseQuery = psprintf(
		"WITH base AS (SELECT groupid, nodeid, noderole::text, nodecluster::text, isactive FROM pg_dist_node WHERE shouldhaveshards ORDER BY groupid, noderole)"
		" SELECT %s.BSON_ARRAY_AGG(%s.row_get_bson(base), 'nodes') FROM base",
		ApiCatalogSchemaName, ApiCatalogSchemaName);

	bool isNull = true;
	Datum nodeDatum = ExtensionExecuteQueryViaSPI(baseQuery, true, SPI_OK_SELECT,
												  &isNull);

	if (isNull)
	{
		return NIL;
	}

	pgbson *queryResult = DatumGetPgBson(nodeDatum);
	pgbsonelement singleElement;
	bson_iter_t arrayIter;
	PgbsonToSinglePgbsonElement(queryResult, &singleElement);
	if (singleElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg(
							"Unexpected - getShardMap path %s should have an array not %s",
							singleElement.path,
							BsonTypeName(singleElement.bsonValue.value_type)),
						errdetail_log(
							"Unexpected - getShardMap path %s should have an array not %s",
							singleElement.path,
							BsonTypeName(singleElement.bsonValue.value_type))));
	}

	BsonValueInitIterator(&singleElement.bsonValue, &arrayIter);

	List *groupMap = NIL;
	int32_t currentGroup = -1;
	while (bson_iter_next(&arrayIter))
	{
		if (!BSON_ITER_HOLDS_DOCUMENT(&arrayIter))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"Unexpected - getShardMap inner groupId %d should have a document not %s",
								currentGroup,
								BsonTypeName(bson_iter_type(&arrayIter))),
							errdetail_log(
								"Unexpected - getShardMap inner groupId %d should have a document not %s",
								currentGroup,
								BsonTypeName(bson_iter_type(&arrayIter)))));
		}

		bson_iter_t objectIter;
		NodeInfo *nodeInfo = palloc0(sizeof(NodeInfo));
		if (bson_iter_recurse(&arrayIter, &objectIter))
		{
			int numFields = 0;
			while (bson_iter_next(&objectIter))
			{
				const char *key = bson_iter_key(&objectIter);
				if (strcmp(key, "groupid") == 0)
				{
					nodeInfo->groupId = bson_iter_int32(&objectIter);
					currentGroup = nodeInfo->groupId;
					numFields++;
				}
				else if (strcmp(key, "nodeid") == 0)
				{
					nodeInfo->nodeId = bson_iter_int32(&objectIter);
					numFields++;
				}
				else if (strcmp(key, "noderole") == 0)
				{
					nodeInfo->nodeRole = bson_iter_dup_utf8(&objectIter, NULL);
					numFields++;
				}
				else if (strcmp(key, "nodecluster") == 0)
				{
					nodeInfo->nodeCluster = bson_iter_dup_utf8(&objectIter, NULL);
					numFields++;
				}
				else if (strcmp(key, "isactive") == 0)
				{
					nodeInfo->isactive = bson_iter_bool(&objectIter);
					numFields++;
				}
			}

			if (numFields != 5)
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
								errmsg(
									"Found missing fields in querying shard table: Found %d fields",
									numFields),
								errdetail_log(
									"Found missing fields in querying shard table: Found %d fields",
									numFields)));
			}

			nodeInfo->mongoNodeName = psprintf("node_%s_%d", nodeInfo->nodeCluster,
											   nodeInfo->nodeId);
			nodeInfo->mongoShardName = psprintf("shard_%d", nodeInfo->groupId);
			groupMap = lappend(groupMap, nodeInfo);
		}
	}

	return groupMap;
}
