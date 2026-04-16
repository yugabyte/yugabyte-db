/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/cluster_operations.c
 *
 * Implementation of a set of cluster operations (e.g. upgrade, initialization, etc).
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
#include "utils/typcache.h"
#include "parser/parse_type.h"
#include "nodes/makefuncs.h"

#include "utils/documentdb_errors.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "utils/guc_utils.h"
#include "utils/query_utils.h"
#include "utils/version_utils.h"
#include "utils/error_utils.h"
#include "utils/version_utils_private.h"
#include "utils/data_table_utils.h"
#include "api_hooks.h"

extern int MaxNumActiveUsersIndexBuilds;
extern int IndexBuildScheduleInSec;
extern char *ApiExtensionName;
extern char *ApiGucPrefix;
extern char *ClusterAdminRole;

char *ApiDistributedSchemaName = "documentdb_api_distributed";
char *DistributedExtensionName = "documentdb_distributed";
bool CreateDistributedFunctions = false;
bool CreateIndexBuildQueueTable = false;

typedef struct ClusterOperationVersions
{
	ExtensionVersion InstalledVersion;
	ExtensionVersion LastUpgradeVersion;
} ClusterOperationVersions;

extern char * GetIndexQueueName(void);

static char * GetClusterInitializedVersion(void);
static void DistributeCrudFunctions(void);
static void ScheduleIndexBuildTasks(char *extensionPrefix);
static void UnscheduleIndexBuildTasks(char *extensionPrefix);
static void CreateIndexBuildsTable(void);
static void CreateValidateDbNameTrigger(void);
static void AlterDefaultDatabaseObjects(void);
static char * UpdateClusterMetadata(bool isInitialize);
static void CreateReferenceTable(const char *tableName);
static void CreateDistributedFunction(const char *functionName, const
									  char *distributionArgName,
									  const char *colocateWith, const
									  char *forceDelegation);
static void DropLegacyChangeStream(void);
static void AddUserColumnsToIndexQueue(void);
static void TriggerInvalidateClusterMetadata(void);
static void AddCollectionsTableViewDefinition(void);
static void AddCollectionsTableValidationColumns(void);
static void CreateExtensionVersionsTrigger(void);
static bool VersionEquals(ExtensionVersion versionA, ExtensionVersion versionB);
static void GetInstalledVersion(ExtensionVersion *installedVersion);
static void ParseVersionString(ExtensionVersion *extensionVersion, char *versionString);
static bool SetupCluster(bool isInitialize);
static void SetPermissionsForReadOnlyRole(void);
static void CheckAndReplicateReferenceTable(const char *schema, const char *tableName);


PG_FUNCTION_INFO_V1(command_initialize_cluster);
PG_FUNCTION_INFO_V1(command_complete_upgrade);

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

	bool isInitialize = true;
	SetupCluster(isInitialize);

	PG_RETURN_VOID();
}


/*
 * command_complete_upgrade executes the necessary steps
 * to perform an extension upgrade.
 */
Datum
command_complete_upgrade(PG_FUNCTION_ARGS)
{
	/* Since complete_upgrade is internal operation, if the disk is full and we have readonly setting on, we should be able to upgrade so we turn off. */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally(psprintf("%s.IsPgReadOnlyForDiskFull", ApiGucPrefix), "false");

	bool isInitialize = false;
	bool upgraded = SetupCluster(isInitialize);

	RollbackGUCChange(savedGUCLevel);

	PG_RETURN_BOOL(upgraded);
}


/*
 * Helper function that checks if the setup scripts for a given extension version must be executed
 * in SetupCluster. It checks whether it's greater than the last upgraded version and less or equal
 * than the current installed version.
 */
static inline bool
ShouldRunSetupForVersion(ClusterOperationVersions *versions,
						 MajorVersion major, int minor, int patch)
{
	return !IsExtensionVersionAtleast(versions->LastUpgradeVersion, major, minor,
									  patch) &&
		   IsExtensionVersionAtleast(versions->InstalledVersion, major, minor, patch);
}


static bool
ShouldRunSetupForVersionForHook(void *versionsVoid,
								int major, int minor, int patch)
{
	return ShouldRunSetupForVersion((ClusterOperationVersions *) versionsVoid,
									(MajorVersion) major, minor, patch);
}


/*
 * Function that runs the necessary steps to initialize and upgrade a cluster.
 */
static bool
SetupCluster(bool isInitialize)
{
	ExtensionVersion lastUpgradeVersion = { 0 };
	ExtensionVersion installedVersion = { 0 };

	/* Ensure that the cluster_data table is replicated on all nodes
	 * otherwise, writes to cluster_data will fail.
	 */
	EnsureMetadataTableReplicated("collections");

	char *lastUpgradeVersionString = UpdateClusterMetadata(isInitialize);
	ParseVersionString(&lastUpgradeVersion, lastUpgradeVersionString);

	GetInstalledVersion(&installedVersion);

	/* For initialize, lastUpgradeVersion will always be 1.0-4, which is the default version for a new cluster until we finish SetupCluster. */
	if (VersionEquals(installedVersion, lastUpgradeVersion))
	{
		ereport(NOTICE, errmsg(
					"version is up-to-date. Skipping function"));
		return false;
	}

	if (!isInitialize)
	{
		ereport(NOTICE, errmsg(
					"Previous Version Major=%d, Minor=%d, Patch=%d; Current Version Major=%d, Minor=%d, Patch=%d",
					lastUpgradeVersion.Major, lastUpgradeVersion.Minor,
					lastUpgradeVersion.Patch,
					installedVersion.Major, installedVersion.Minor,
					installedVersion.Patch));
	}

	ClusterOperationVersions versions =
	{
		.InstalledVersion = installedVersion,
		.LastUpgradeVersion = lastUpgradeVersion
	};

	/* If the version is < 0.0-5 or if it's an initialize ensure metadata collections are created */
	if (isInitialize ||
		ShouldRunSetupForVersion(&versions, DocDB_V0, 0, 5))
	{
		/*
		 * We should only create and modify schema objects here for versions that are no longer covered by the upgrade path.
		 */
		StringInfo relationName = makeStringInfo();
		appendStringInfo(relationName, "%s.collections", ApiCatalogSchemaName);
		CreateReferenceTable(relationName->data);

		resetStringInfo(relationName);
		appendStringInfo(relationName, "%s.collection_indexes", ApiCatalogSchemaName);
		CreateReferenceTable(relationName->data);
		DistributeCrudFunctions();

		CreateValidateDbNameTrigger();

		/* As of 1.23 the schema installs the type columns. */
		if (!IsExtensionVersionAtleast(installedVersion, DocDB_V0, 23, 0))
		{
			AlterDefaultDatabaseObjects();
		}

		resetStringInfo(relationName);
		appendStringInfo(relationName, "%s.%s_cluster_data", ApiDistributedSchemaName,
						 ExtensionObjectPrefix);
		CreateReferenceTable(relationName->data);
	}

	/*
	 * For initialize, lastUpgradeVersion will always be 1.4-0, so all of the below conditions will apply if the installedVersion meets the requirement.
	 */
	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 7, 0))
	{
		AddCollectionsTableViewDefinition();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 7, 0) &&
		!ShouldRunSetupForVersion(&versions, DocDB_V0, 12, 0))
	{
		/* Schedule happens again at 1.12 */
		ScheduleIndexBuildTasks(ExtensionObjectPrefix);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 8, 0))
	{
		CreateExtensionVersionsTrigger();

		/* We invalidate the cache in order to enable the extension versions trigger we just created. */
		TriggerInvalidateClusterMetadata();
		AddCollectionsTableValidationColumns();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 12, 0))
	{
		CreateIndexBuildsTable();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 12, 0) &&
		!ShouldRunSetupForVersion(&versions, DocDB_V0, 15, 0))
	{
		/* Unschedule index tasks from old queue. */
		char *oldExtensionPrefix = ExtensionObjectPrefix;
		UnscheduleIndexBuildTasks(oldExtensionPrefix);

		char *extensionPrefix = ExtensionObjectPrefixV2;
		ScheduleIndexBuildTasks(extensionPrefix);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 14, 0))
	{
		DropLegacyChangeStream();
		AddUserColumnsToIndexQueue();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 15, 0))
	{
		/* reduce the Index background cron job schedule to 2 seconds by default. */
		char *extensionPrefix = ExtensionObjectPrefixV2;
		UnscheduleIndexBuildTasks(extensionPrefix);
		ScheduleIndexBuildTasks(extensionPrefix);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 17, 1))
	{
		SetPermissionsForReadOnlyRole();
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 21, 0))
	{
		if (!isInitialize && ClusterAdminRole[0] != '\0')
		{
			StringInfo cmdStr = makeStringInfo();
			bool isNull = false;
			appendStringInfo(cmdStr,
							 "GRANT %s, %s TO %s WITH ADMIN OPTION;",
							 ApiAdminRoleV2,
							 ApiReadOnlyRole,
							 quote_identifier(ClusterAdminRole));
			ExtensionExecuteQueryViaSPI(cmdStr->data, false, SPI_OK_UTILITY,
										&isNull);
		}
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 23, 0))
	{
		/* Re-add the primary key in the context of the cluster operations. */
		StringInfo cmdStr = makeStringInfo();
		bool isNull = false;
		appendStringInfo(cmdStr,
						 "ALTER TABLE %s.%s_cluster_data DROP CONSTRAINT IF EXISTS %s_cluster_data_pkey",
						 ApiDistributedSchemaName, ExtensionObjectPrefix,
						 ExtensionObjectPrefix);
		ExtensionExecuteQueryViaSPI(cmdStr->data, false, SPI_OK_UTILITY,
									&isNull);

		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "ALTER TABLE %s.%s_cluster_data ADD PRIMARY KEY(metadata)",
						 ApiDistributedSchemaName, ExtensionObjectPrefix);
		ExtensionExecuteQueryViaSPI(cmdStr->data, false, SPI_OK_UTILITY,
									&isNull);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 23, 2))
	{
		CheckAndReplicateReferenceTable(ApiCatalogSchemaName, "collections");
		CheckAndReplicateReferenceTable(ApiCatalogSchemaName, "collection_indexes");

		StringInfo relationName = makeStringInfo();
		appendStringInfo(relationName, "%s_cluster_data", ExtensionObjectPrefix);
		CheckAndReplicateReferenceTable(ApiDistributedSchemaName, relationName->data);
	}

	if (ShouldRunSetupForVersion(&versions, DocDB_V0, 101, 0))
	{
		AlterCreationTime();
	}

	/* we call the post setup cluster hook to allow the extension to do any additional setup */
	PostSetupClusterHook(isInitialize, &ShouldRunSetupForVersionForHook, &versions);

	TriggerInvalidateClusterMetadata();
	return true;
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
					 "SELECT %s.bson_get_value_text(metadata, 'initialized_version') FROM "
					 "%s.%s_cluster_data;", CoreSchemaName, ApiDistributedSchemaName,
					 ExtensionObjectPrefix);

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
	int shardCount = 0;
	DistributePostgresTable(changesRelation, distributionColumn, colocateWith,
							shardCount);


	if (!CreateDistributedFunctions)
	{
		return;
	}

	StringInfo relationName = makeStringInfo();

	/* Push down the delete/insert/update one function calls */
	appendStringInfo(relationName,
					 "%s.delete_one(bigint,bigint,%s,%s,bool,%s,text)",
					 ApiInternalSchemaName, FullBsonTypeName, FullBsonTypeName,
					 FullBsonTypeName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);

	resetStringInfo(relationName);
	appendStringInfo(relationName, "%s.insert_one(bigint,bigint,%s,text)",
					 ApiInternalSchemaName, FullBsonTypeName);
	CreateDistributedFunction(
		relationName->data,
		distributionArgName,
		changesRelation,
		forceDelegation
		);

	resetStringInfo(relationName);
	appendStringInfo(relationName,
					 "%s.update_one(bigint,bigint,%s,%s,%s,bool,%s,bool,%s,%s,text)",
					 ApiInternalSchemaName, FullBsonTypeName, FullBsonTypeName,
					 FullBsonTypeName,
					 FullBsonTypeName, FullBsonTypeName, FullBsonTypeName);
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
ScheduleIndexBuildTasks(char *extensionPrefix)
{
	char scheduleInterval[50];
	if (IndexBuildScheduleInSec < 60)
	{
		sprintf(scheduleInterval, "%d seconds", IndexBuildScheduleInSec);
	}
	else
	{
		sprintf(scheduleInterval, "* * * * *");
	}

	bool isNull = false;
	bool readOnly = false;

	const int maxActiveIndexBuilds = MaxNumActiveUsersIndexBuilds;
	for (int i = 1; i <= maxActiveIndexBuilds; i++)
	{
		StringInfo scheduleStr = makeStringInfo();
		appendStringInfo(scheduleStr,
						 "SELECT cron.schedule('%s_index_build_task_'"
						 " || %d, '%s',"
						 "'CALL %s.build_index_concurrently(%d);');",
						 extensionPrefix, i, scheduleInterval,
						 ApiInternalSchemaName, i);
		ExtensionExecuteQueryViaSPI(scheduleStr->data, readOnly, SPI_OK_SELECT,
									&isNull);
	}
}


/*
 * Unschedule background jobs for index creation.
 */
static void
UnscheduleIndexBuildTasks(char *extensionPrefix)
{
	bool isNull = false;
	bool readOnly = false;

	/*
	 * These schedule the index build tasks at the coordinator.
	 * Since we leave behind the jobs when dropping the extension (during development), it would be nice to unschedule
	 * existing ones first in case something changed.
	 * PS: We need to run this with array_agg because otherwise the SPI API would only execute unschedule for the first job.
	 */
	StringInfo unscheduleStr = makeStringInfo();
	appendStringInfo(unscheduleStr,
					 "SELECT array_agg(cron.unschedule(jobid)) FROM cron.job WHERE jobname LIKE"
					 "'%s_index_build_task%%';", extensionPrefix);
	ExtensionExecuteQueryViaSPI(unscheduleStr->data, readOnly, SPI_OK_SELECT,
								&isNull);
}


static void
CreateIndexBuildQueueCore()
{
	bool readOnly = false;
	bool isNull = false;

	StringInfo dropStr = makeStringInfo();
	appendStringInfo(dropStr,
					 "DROP TABLE IF EXISTS %s;", GetIndexQueueName());
	ExtensionExecuteQueryViaSPI(dropStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	StringInfo createStr = makeStringInfo();
	appendStringInfo(createStr,
					 "CREATE TABLE IF NOT EXISTS %s ("
					 "index_cmd text not null,"

	                 /* 'C' for CREATE INDEX and 'R' for REINDEX */
					 "cmd_type char CHECK (cmd_type IN ('C', 'R')),"
					 "index_id integer not null,"

	                 /* index_cmd_status gets represented as enum IndexCmdStatus in index.h */
					 "index_cmd_status integer default 1,"
					 "global_pid bigint,"
					 "start_time timestamp WITH TIME ZONE,"
					 "collection_id bigint not null,"

	                 /* Used to enter the error encounter during execution of index_cmd */
					 "comment %s.bson,"

	                 /* current attempt counter for retrying the failed request */
					 "attempt smallint,"

	                 /* update_time shows the time when request was updated in the table */
					 "update_time timestamp with time zone DEFAULT now()"

					 ")", GetIndexQueueName(), CoreSchemaName);

	ExtensionExecuteQueryViaSPI(createStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(createStr);
	appendStringInfo(createStr,
					 "CREATE INDEX IF NOT EXISTS %s_index_queue_indexid_cmdtype on %s (index_id, cmd_type)",
					 ExtensionObjectPrefixV2, GetIndexQueueName());
	ExtensionExecuteQueryViaSPI(createStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(createStr);
	appendStringInfo(createStr,
					 "CREATE INDEX IF NOT EXISTS %s_index_queue_cmdtype_collectionid_cmdstatus on %s (cmd_type, collection_id, index_cmd_status)",
					 ExtensionObjectPrefixV2, GetIndexQueueName());
	ExtensionExecuteQueryViaSPI(createStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(createStr);
	appendStringInfo(createStr,
					 "GRANT SELECT ON TABLE %s TO public", GetIndexQueueName());
	ExtensionExecuteQueryViaSPI(createStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(createStr);
	appendStringInfo(createStr,
					 "GRANT ALL ON TABLE %s TO %s, %s",
					 GetIndexQueueName(),
					 ApiAdminRoleV2,
					 ApiAdminRole);
	ExtensionExecuteQueryViaSPI(createStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Create index queue table, its indexes and grant permissions.
 */
static void
CreateIndexBuildsTable()
{
	/* Create index builds table if legacy compat */
	if (CreateIndexBuildQueueTable)
	{
		CreateIndexBuildQueueCore();
	}

	bool readOnly = false;
	bool isNull = false;
	StringInfo createStr = makeStringInfo();
	appendStringInfo(createStr,
					 "SELECT citus_add_local_table_to_metadata('%s')",
					 GetIndexQueueName());
	ExtensionExecuteQueryViaSPI(createStr->data, readOnly, SPI_OK_SELECT,
								&isNull);
}


/*
 * Create validate_dbname trigger on the collections table.
 */
static void
CreateValidateDbNameTrigger()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "CREATE OR REPLACE TRIGGER collections_trigger_validate_dbname "
					 "BEFORE INSERT OR UPDATE ON %s.collections "
					 "FOR EACH ROW EXECUTE FUNCTION "
					 "%s.trigger_validate_dbname();", ApiCatalogSchemaName,
					 ApiCatalogToApiInternalSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Handle failures if the worker has the attribute.
 * This handles mixed schema versioning.
 */
static bool
AddAttributeHandleIfExists(const char *addAttributeQuery)
{
	volatile bool isSuccess = false;

	/* use a subtransaction to correctly handle failures */
	MemoryContext oldContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;
	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		bool readOnly = false;
		bool isNull = false;
		ExtensionExecuteQueryViaSPI(addAttributeQuery, readOnly, SPI_OK_UTILITY,
									&isNull);

		/* Commit the inner transaction, return to outer xact context */
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;
		isSuccess = true;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(oldContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();

		/* Rollback changes MemoryContext */
		MemoryContextSwitchTo(oldContext);
		CurrentResourceOwner = oldOwner;

		if (errorData->sqlerrcode != ERRCODE_DUPLICATE_COLUMN)
		{
			ReThrowError(errorData);
		}

		isSuccess = false;
	}
	PG_END_TRY();

	return isSuccess;
}


/*
 * Change internal tables to include new fields and constraints required by the extension.
 * TODO: Remove this after Cluster Version 1.23-0
 */
static void
AlterDefaultDatabaseObjects()
{
	StringInfo cmdStr = makeStringInfo();

	/* -- We do the ALTER TYPE in the Initialize/Complete function so that we handle the */
	/* -- upgrade scenarios where worker/coordinator are in mixed versions. Ser/Der/Casting would */
	/* -- fail if any one upgrades first and we did the ALTER TYPE in the extension upgrade. */
	/* -- by doing this in the initialize/complete, we guarantee it happens once from the DDL */
	/* -- coordinator and it's transactional. */
	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TYPE %s.index_spec_type_internal ADD ATTRIBUTE cosmos_search_options %s.bson;",
					 ApiCatalogSchemaName, CoreSchemaName);
	bool attributeAdded = AddAttributeHandleIfExists(cmdStr->data);

	if (!attributeAdded)
	{
		/* Scenario where worker has it but coordinator doesn't, disable DDL propagation and try again */
		int gucLevel = NewGUCNestLevel();
		SetGUCLocally("citus.enable_ddl_propagation", "off");
		AddAttributeHandleIfExists(cmdStr->data);
		RollbackGUCChange(gucLevel);
	}

	/* -- all new options will go into this one bson field. */
	/* -- Older options will be cleaned up in a separate release. */
	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TYPE %s.index_spec_type_internal ADD ATTRIBUTE index_options %s.bson;",
					 ApiCatalogSchemaName, CoreSchemaName);
	attributeAdded = AddAttributeHandleIfExists(cmdStr->data);
	if (!attributeAdded)
	{
		/* Scenario where worker has it but coordinator doesn't, disable DDL propagation and try again */
		int gucLevel = NewGUCNestLevel();
		SetGUCLocally("citus.enable_ddl_propagation", "off");
		AddAttributeHandleIfExists(cmdStr->data);
		RollbackGUCChange(gucLevel);
	}
}


/*
 * Adds bson column view_definition to the collections table.
 */
static void
AddCollectionsTableViewDefinition()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s.collections ADD IF NOT EXISTS view_definition "
					 "%s.bson default null;", ApiCatalogSchemaName, CoreSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Add schema validation columns to the collections table.
 */
static void
AddCollectionsTableValidationColumns()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s.collections "
					 "ADD COLUMN IF NOT EXISTS validator %s.bson DEFAULT null, "
					 "ADD COLUMN IF NOT EXISTS validation_level text DEFAULT null CONSTRAINT validation_level_check CHECK (validation_level IN ('off', 'strict', 'moderate')), "
					 "ADD COLUMN IF NOT EXISTS validation_action text DEFAULT null CONSTRAINT validation_action_check CHECK (validation_action IN ('warn', 'error'));",
					 ApiCatalogSchemaName, CoreSchemaName);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Creates trigger for updates or deletes in the cluster_data table from the catalog schema.
 */
static void
CreateExtensionVersionsTrigger()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "CREATE TRIGGER %s_versions_trigger "
					 "AFTER UPDATE OR DELETE ON "
					 "%s.%s_cluster_data "
					 "FOR STATEMENT EXECUTE FUNCTION "
					 "%s.update_%s_version_data();",
					 ExtensionObjectPrefix, ApiDistributedSchemaName,
					 ExtensionObjectPrefix, ApiInternalSchemaName,
					 ExtensionObjectPrefix);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
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
 * Cleaning change_stream related constructs that were used for backward compatibility.
 */
static void
DropLegacyChangeStream()
{
	bool readOnly = false;
	bool isNull = false;

	ArrayType *arrayValue = GetCollectionIds();
	if (arrayValue == NULL)
	{
		return;
	}

	StringInfo cmdStr = makeStringInfo();
	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT8OID, sizeof(int64), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	for (int i = 0; i < numElements; i++)
	{
		int64_t collection_id = DatumGetInt64(elements[i]);

		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "ALTER TABLE IF EXISTS %s.documents_%ld DROP COLUMN IF EXISTS change_description;",
						 ApiDataSchemaName, collection_id);
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
									&isNull);

		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "DROP TRIGGER IF EXISTS record_changes_trigger ON %s.documents_%ld;",
						 ApiDataSchemaName, collection_id);
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
									&isNull);
	}
}


/*
 * Add user_oid colum and its constraints to the index queue table.
 */
void
AddUserColumnsToIndexQueue()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s ADD COLUMN IF NOT EXISTS user_oid Oid;",
					 GetIndexQueueName());
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	/* We first drop the check constraint if it already exists. Some upgrade paths can create it before this function is executed. */
	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s DROP CONSTRAINT IF EXISTS %s_index_queue_user_oid_check;",
					 GetIndexQueueName(), ExtensionObjectPrefixV2);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(cmdStr);
	appendStringInfo(cmdStr,
					 "ALTER TABLE %s ADD CONSTRAINT %s_index_queue_user_oid_check CHECK (user_oid IS NULL OR user_oid != '0'::oid);",
					 GetIndexQueueName(), ExtensionObjectPrefixV2);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);
}


/*
 * Invalidate the cluster version metadata cache for all active processes.
 */
static void
TriggerInvalidateClusterMetadata()
{
	bool isNull = false;
	bool readOnly = false;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "UPDATE %s.%s_cluster_data SET metadata = metadata;",
					 ApiDistributedSchemaName, ExtensionObjectPrefix);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UPDATE,
								&isNull);
}


/*
 * Utility funtion that checks whether 2 ExtensionVersion objects are equal (i.e. both store the same version number)
 */
static bool
VersionEquals(ExtensionVersion versionA, ExtensionVersion versionB)
{
	return versionA.Major == versionB.Major &&
		   versionA.Minor == versionB.Minor &&
		   versionA.Patch == versionB.Patch;
}


/*
 * Receives an ExtensionVersion object and populates it with the contents of a version string in the form of "Major.Minor-Patch".
 */
static void
ParseVersionString(ExtensionVersion *extensionVersion, char *versionString)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT regexp_split_to_array(TRIM(BOTH '\"' FROM '%s'), '[-\\.]')::int4[];",
					 versionString);

	bool readOnly = true;
	bool isNull = false;
	Datum versionDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
													 SPI_OK_SELECT, &isNull);

	ArrayType *arrayValue = DatumGetArrayTypeP(versionDatum);

	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT4OID, sizeof(int), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	Assert(numElements == 3);
	extensionVersion->Major = DatumGetInt32(elements[0]);
	extensionVersion->Minor = DatumGetInt32(elements[1]);
	extensionVersion->Patch = DatumGetInt32(elements[2]);
}


/*
 * Receives an ExtensionVersion object and populates it with the current installed version of the extension.
 */
static void
GetInstalledVersion(ExtensionVersion *installedVersion)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT regexp_split_to_array((SELECT extversion FROM pg_extension WHERE extname = '%s'), '[-\\.]')::int4[];",
					 ApiExtensionName);

	bool readOnly = true;
	bool isNull = false;
	Datum versionDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly,
													 SPI_OK_SELECT, &isNull);

	ArrayType *arrayValue = DatumGetArrayTypeP(versionDatum);

	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT4OID, sizeof(int), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	Assert(numElements == 3);
	installedVersion->Major = DatumGetInt32(elements[0]);
	installedVersion->Minor = DatumGetInt32(elements[1]);
	installedVersion->Patch = DatumGetInt32(elements[2]);
}


/*
 * SetPermissionsForReadOnlyRole - Set the right permissions for ApiReadOnlyRole
 */
static void
SetPermissionsForReadOnlyRole()
{
	bool readOnly = false;
	bool isNull = false;
	StringInfo cmdStr = makeStringInfo();

	appendStringInfo(cmdStr,
					 "GRANT SELECT ON TABLE %s.%s_cluster_data TO %s;",
					 ApiDistributedSchemaName, ExtensionObjectPrefix, ApiReadOnlyRole);
	ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	ArrayType *arrayValue = GetCollectionIds();
	if (arrayValue == NULL)
	{
		return;
	}

	Datum *elements = NULL;
	int numElements = 0;
	bool *val_is_null_marker;
	deconstruct_array(arrayValue, INT8OID, sizeof(int64), true, TYPALIGN_INT,
					  &elements, &val_is_null_marker, &numElements);

	for (int i = 0; i < numElements; i++)
	{
		int64_t collection_id = DatumGetInt64(elements[i]);
		resetStringInfo(cmdStr);
		appendStringInfo(cmdStr,
						 "GRANT SELECT ON %s.documents_%ld TO %s;",
						 ApiDataSchemaName, collection_id, ApiReadOnlyRole);
		ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_UTILITY,
									&isNull);
	}
}


/*
 * Checks if the given reference table is replicated.
 */
static void
CheckAndReplicateReferenceTable(const char *schema, const char *tableName)
{
	StringInfo relationName = makeStringInfo();
	appendStringInfo(relationName, "%s_%s", ExtensionObjectPrefix, "cluster_data");
	if (!(strcmp(tableName, "collections") == 0 ||
		  strcmp(tableName, "collection_indexes") == 0 ||
		  strcmp(tableName, relationName->data) == 0))
	{
		return;
	}

	StringInfo queryStringInfo = makeStringInfo();
	appendStringInfo(queryStringInfo,
					 "SELECT shardid FROM pg_catalog.pg_dist_shard WHERE logicalrelid = '%s.%s'::regclass",
					 schema, tableName);

	bool isNull = false;
	ExtensionExecuteQueryViaSPI(queryStringInfo->data, false, SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		/* Not replicated, replicate it.*/
		resetStringInfo(relationName);
		appendStringInfo(relationName, "%s.%s", schema, tableName);
		CreateReferenceTable(relationName->data);
	}
}


static char *
UpdateClusterMetadata(bool isInitialize)
{
	bool isNull = false;

	Datum args[1] = { CStringGetTextDatum(DistributedExtensionName) };
	Oid argTypes[1] = { TEXTOID };
	Datum catalogExtVersion = ExtensionExecuteQueryWithArgsViaSPI(
		"SELECT extversion FROM pg_extension WHERE extname = $1", 1, argTypes, args, NULL,
		true, SPI_OK_SELECT, &isNull);
	Assert(!isNull);

	Datum clusterVersionDatum = ExtensionExecuteQueryViaSPI(
		FormatSqlQuery(
			"SELECT %s.bson_get_value_text(metadata, 'last_deploy_version') FROM %s.%s_cluster_data",
			CoreSchemaNameV2, ApiDistributedSchemaName, ExtensionObjectPrefix),
		true,
		SPI_OK_SELECT,
		&isNull);
	Assert(!isNull);

	char *catalogVersion = TextDatumGetCString(catalogExtVersion);
	char *clusterVersion = TextDatumGetCString(clusterVersionDatum);

	if (strcmp(clusterVersion, catalogVersion) == 0)
	{
		elog(NOTICE, "version is up-to-date. Skipping function");
		return clusterVersion;
	}

	/*  get Citus version. */
	Datum citusVersion = ExtensionExecuteQueryViaSPI(
		"SELECT coalesce(metadata->>'last_upgrade_version', '11.0-1') FROM pg_dist_node_metadata",
		true, SPI_OK_SELECT, &isNull);
	Assert(!isNull);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendUtf8(&writer, "last_deploy_version", -1, catalogVersion);
	PgbsonWriterAppendUtf8(&writer, "last_citus_version", -1, TextDatumGetCString(
							   citusVersion));

	/* Seed the first version */
	if (isInitialize)
	{
		PgbsonWriterAppendUtf8(&writer, "initialized_version", -1, catalogVersion);
	}

	Datum updateArgs[1] = { PointerGetDatum(PgbsonWriterGetPgbson(&writer)) };
	Oid updateTypes[1] = { BsonTypeId() };

	/*  Update the row */
	const char *updateQuery = FormatSqlQuery(
		"UPDATE %s.%s_cluster_data SET metadata = %s.bson_dollar_set(metadata, $1)",
		ApiDistributedSchemaName, ExtensionObjectPrefix, ApiCatalogSchemaName);
	ExtensionExecuteQueryWithArgsViaSPI(updateQuery, 1, updateTypes, updateArgs, NULL,
										false, SPI_OK_UPDATE, &isNull);
	return clusterVersion;
}
