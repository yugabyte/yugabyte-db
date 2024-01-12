/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/initialize_cluster.c
 *
 * Implementation of the initialize_cluster UDF.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/resowner.h"
#include "lib/stringinfo.h"
#include "access/xact.h"

#include "utils/mongo_errors.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "utils/query_utils.h"

#include "api_hooks.h"

extern int MaxNumActiveUsersIndexBuilds;
extern int IndexBuildScheduleInSec;

static char * GetClusterInitializedVersion(void);
static void DistributeCrudFunctions(void);
static void ScheduleIndexBuildTasks(void);
static void CreateDatabaseTriggers(void);
static void AlterDefaultDatabaseObjects(void);
static void UpdateClusterMetadata(void);
static void CreateReferenceTable(const char *tableName);
static void CreateDistributedFunction(const char *functionName, const
									  char *distributionArgName,
									  const char *colocateWith, const
									  char *forceDelegation);
static void InvalidateClusterMetadata(void);

PG_FUNCTION_INFO_V1(command_initialize_cluster);

/*
 * command_initialize_cluster implements the core
 * logic to initialize the extension in the cluster
 */
Datum
command_initialize_cluster(PG_FUNCTION_ARGS)
{
	char *initializedVersion = GetClusterInitializedVersion();
	if (initializedVersion != NULL)
	{
		ereport(NOTICE, errmsg(
					"Initialize: version is up-to-date. Skipping initialize_cluster"));
		PG_RETURN_VOID();
	}

	/* Make the catalogs available from all nodes */
	StringInfo relationName = makeStringInfo();
	appendStringInfo(relationName, "%s.collections", ApiCatalogSchemaName);
	CreateReferenceTable(relationName->data);

	resetStringInfo(relationName);
	appendStringInfo(relationName, "%s.collection_indexes", ApiCatalogSchemaName);
	CreateReferenceTable(relationName->data);

	DistributeCrudFunctions();
	ScheduleIndexBuildTasks();
	CreateDatabaseTriggers();
	AlterDefaultDatabaseObjects();

	UpdateClusterMetadata();
	InvalidateClusterMetadata();

	PG_RETURN_VOID();
}


/*
 * Returns the current installed version of the extension in the cluster. If it's not installed,
 * then NULL is returned.
 */
static char *
GetClusterInitializedVersion()
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT metadata->>'initialized_version' FROM "
					 "%s.%s_cluster_data;", ApiCatalogSchemaName, ExtensionObjectPrefix);

	bool isNull = false;
	bool readOnly = true;
	Datum resultDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
													&isNull);
	if (isNull)
	{
		return NULL;
	}
	return text_to_cstring(DatumGetTextP(resultDatum));
}


/*
 * Creates all distributed objects required by the extension to run in the cluster.
 */
static void
DistributeCrudFunctions()
{
	/* TODO: when we move to OSS revisit change stream stuff. */
	/* Table is distributed and co-located with the collections it is tracking */
	const char *distributionArgName = "p_shard_key_value";
	const char *forceDelegation = "true";

	char changesRelation[50];
	sprintf(changesRelation, "%s.changes", ApiDataSchemaName);
	const char *distributionColumn = "shard_key_value";
	const char *colocateWith = "none";
	DistributePostgresTable(changesRelation, distributionColumn, colocateWith);

	StringInfo relationName = makeStringInfo();

	/* Push down the delete/insert/update one function calls */
	appendStringInfo(relationName,
					 "%s.delete_one(bigint,bigint,bson,bson,bool,bson,text)",
					 ApiInternalSchemaName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);

	resetStringInfo(relationName);
	appendStringInfo(relationName, "%s.insert_one(bigint,bigint,bson,text)",
					 ApiInternalSchemaName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);

	resetStringInfo(relationName);
	appendStringInfo(relationName,
					 "%s.update_one(bigint,bigint,bson,bson,bson,bool,bson,bool,bson,bson,text)",
					 ApiInternalSchemaName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);
}


/*
 * Schedule background jobs that will later be used to create indexes in the cluster.
 */
static void
ScheduleIndexBuildTasks()
{
	bool isNull = false;
	bool readOnly = false;

	/*
	 * These schedule the index build tasks at the coordinator.
	 * TODO: Decide on value of MaxNumActiveUsersIndexBuilds. Also how will user override these ?
	 *
	 * Since we leave behind the jobs when dropping the extension (during development), it would be nice to unschedule existing ones first in case something changed
	 */
	StringInfo unscheduleStr = makeStringInfo();
	appendStringInfo(unscheduleStr,
					 "SELECT cron.unschedule(jobid) FROM cron.job WHERE jobname LIKE"
					 "'%s_index_build_task%%';", ExtensionObjectPrefix);
	ExtensionExecuteQueryViaSPI(unscheduleStr->data, readOnly, SPI_OK_SELECT,
								&isNull);

	char scheduleInterval[50];

	if (IndexBuildScheduleInSec < 60)
	{
		sprintf(scheduleInterval, "%d seconds", IndexBuildScheduleInSec);
	}
	else
	{
		sprintf(scheduleInterval, "* * * * *");
	}

	const int maxActiveIndexBuilds = MaxNumActiveUsersIndexBuilds;
	for (int i = 1; i <= maxActiveIndexBuilds; i++)
	{
		StringInfo scheduleStr = makeStringInfo();
		appendStringInfo(scheduleStr,
						 "SELECT cron.schedule('%s_index_build_task_'"
						 " || %d, '%s',"
						 "'CALL %s.build_index_concurrently(%d);');",
						 ExtensionObjectPrefix, i, scheduleInterval,
						 ApiInternalSchemaName, i);
		ExtensionExecuteQueryViaSPI(scheduleStr->data, readOnly, SPI_OK_SELECT,
									&isNull);
	}
}


/*
 * Create database triggers that will be executed upon CRUD operations in internal tables.
 */
static void
CreateDatabaseTriggers()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "CREATE TRIGGER collections_trigger_validate_dbname "
					 "BEFORE INSERT OR UPDATE ON %s.collections "
					 "FOR EACH ROW EXECUTE FUNCTION "
					 "%s.trigger_validate_dbname();", ApiCatalogSchemaName,
					 ApiCatalogSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "CREATE TRIGGER %s_versions_trigger "
					 "AFTER UPDATE OR DELETE ON "
					 "%s.%s_cluster_data "
					 "FOR STATEMENT EXECUTE FUNCTION "
					 "%s.update_%s_version_data();",
					 ExtensionObjectPrefix, ApiCatalogSchemaName,
					 ExtensionObjectPrefix, ApiInternalSchemaName, ExtensionObjectPrefix);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Change internal tables to include new fields and constraints required by the extension.
 */
static void
AlterDefaultDatabaseObjects()
{
	bool isNull = false;
	bool readOnly = false;
	StringInfo cmdStr = makeStringInfo();

	/* -- We do the ALTER TYPE in the Initialize/Complete function so that we handle the */
	/* -- upgrade scenarios where worker/coordinator are in mixed versions. Ser/Der/Casting would */
	/* -- fail if any one upgrades first and we did the ALTER TYPE in the extension upgrade. */
	/* -- by doing this in the initialize/complete, we guarantee it happens once from the DDL */
	/* -- coordinator and it's transactional. */
	appendStringInfo(cmdStr,
					 "ALTER TYPE %s.index_spec_type_internal ADD ATTRIBUTE cosmos_search_options bson;",
					 ApiCatalogSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	/* -- all new options will go into this one bson field. */
	/* -- Older options will be cleaned up in a separate release. */
	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TYPE %s.index_spec_type_internal ADD ATTRIBUTE index_options bson;",
					 ApiCatalogSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s.collections ADD view_definition "
					 "%s.bson default null;", ApiCatalogSchemaName, CoreSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	/* -- Add new columns in the collections table for Schema Validation */
	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s.collections "
					 "ADD COLUMN validator %s.bson DEFAULT null, "
					 "ADD COLUMN validation_level text DEFAULT null CONSTRAINT validation_level_check CHECK (validation_level IN ('off', 'strict', 'moderate')), "
					 "ADD COLUMN validation_action text DEFAULT null CONSTRAINT validation_action_check CHECK (validation_action IN ('warn', 'error'));",
					 ApiCatalogSchemaName, CoreSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Update cluster metadata and distribute the metadata table.
 */
static void
UpdateClusterMetadata()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT %s.update_cluster_metadata(p_isInitialize => true);",
					 ApiInternalSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
								&isNull);

	StringInfo relationName = makeStringInfo();
	appendStringInfo(relationName, "%s.%s_cluster_data", ApiCatalogSchemaName,
					 ExtensionObjectPrefix);
	CreateReferenceTable(relationName->data);
}


/*
 * Create a distributed reference table.
 */
static void
CreateReferenceTable(const char *tableName)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT create_reference_table('%s');",
					 tableName);

	bool isNull = false;
	bool readOnly = false;
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
								&isNull);
}


/*
 * Create a distributed function.
 */
static void
CreateDistributedFunction(const char *functionName, const char *distributionArgName,
						  const char *colocateWith, const char *forceDelegation)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT create_distributed_function('%s', '%s', colocate_with := '%s', force_delegation := %s);",
					 functionName,
					 distributionArgName,
					 colocateWith,
					 forceDelegation);

	bool isNull = false;
	bool readOnly = false;
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
								&isNull);
}


/*
 * Invalidate the cluster version metadata.
 */
static void
InvalidateClusterMetadata()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "UPDATE %s.pgmongo_cluster_data SET metadata = metadata;",
					 ApiCatalogSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UPDATE,
								&isNull);
}
