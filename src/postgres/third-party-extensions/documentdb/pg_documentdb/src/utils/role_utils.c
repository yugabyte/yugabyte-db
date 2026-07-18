/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/role_utils.c
 *
 * Implementation of role utility functions.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "common/saslprep.h"
#include "common/scram-common.h"
#include "commands/commands_common.h"
#include "commands/parse_error.h"
#include "libpq/scram.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"
#include "utils/documentdb_errors.h"
#include "utils/feature_counter.h"
#include "utils/hashset_utils.h"
#include "utils/list_utils.h"
#include "utils/query_utils.h"
#include "utils/role_utils.h"
#include "utils/string_view.h"
#include "api_hooks.h"
#include "api_hooks_def.h"

#define SCRAM_MAX_SALT_LEN 64

/* GUC to enable user crud operations */
extern bool EnableUserCrud;

/* GUC that controls the default salt length*/
extern int ScramDefaultSaltLen;

/* GUC that controls the max number of users allowed*/
extern int MaxUserLimit;

/* GUC that controls the blocked role prefix list*/
extern char *BlockedRolePrefixList;

/* GUC that controls whether we use username/password validation*/
extern bool EnableUsernamePasswordConstraints;

/* GUC that controls whether the usersInfo command returns privileges*/
extern bool EnableUsersInfoPrivileges;

/* GUC that controls whether native authentication is enabled*/
extern bool IsNativeAuthEnabled;

static void WriteSinglePrivilegeDocument(const ConsolidatedPrivilege *privilege,
										 pgbson_array_writer *privilegesArrayWriter);
static void ConsolidatePrivileges(List **consolidatedPrivileges,
								  const Privilege *sourcePrivileges,
								  size_t sourcePrivilegeCount);
static void ConsolidatePrivilege(List **consolidatedPrivileges,
								 const Privilege *sourcePrivilege);
static bool ComparePrivileges(const ConsolidatedPrivilege *privilege1,
							  const Privilege *privilege2);
static void ConsolidatePrivilegesForRole(const char *roleName,
										 List **consolidatedPrivileges);
static void WritePrivilegeListToArray(List *consolidatedPrivileges,
									  pgbson_array_writer *privilegesArrayWriter);
static void DeepFreePrivileges(List *consolidatedPrivileges);

/*
 * Static definitions for user privileges and roles
 * These are used to define the privileges associated with each role
 */
static const Privilege readOnlyPrivileges[] = {
	{
		.db = "",
		.collection = "",
		.isCluster = false,
		.numActions = 7,
		.actions = (const StringView[]) {
			{ .string = "changeStream", .length = 12 },
			{ .string = "collStats", .length = 9 },
			{ .string = "dbStats", .length = 7 },
			{ .string = "find", .length = 4 },
			{ .string = "killCursors", .length = 11 },
			{ .string = "listCollections", .length = 15 },
			{ .string = "listIndexes", .length = 11 }
		}
	},
	{
		.db = "",
		.collection = "",
		.isCluster = true,
		.numActions = 1,
		.actions = (const StringView[]) {
			{ .string = "listDatabases", .length = 13 }
		}
	}
};

static const Privilege readWritePrivileges[] = {
	{
		.db = "",
		.collection = "",
		.isCluster = false,
		.numActions = 14,
		.actions = (const StringView[]) {
			{ .string = "changeStream", .length = 12 },
			{ .string = "collStats", .length = 9 },
			{ .string = "createCollection", .length = 16 },
			{ .string = "createIndex", .length = 11 },
			{ .string = "dbStats", .length = 7 },
			{ .string = "dropCollection", .length = 14 },
			{ .string = "dropIndex", .length = 9 },
			{ .string = "find", .length = 4 },
			{ .string = "insert", .length = 6 },
			{ .string = "killCursors", .length = 11 },
			{ .string = "listCollections", .length = 15 },
			{ .string = "listIndexes", .length = 11 },
			{ .string = "remove", .length = 6 },
			{ .string = "update", .length = 6 }
		}
	},
	{
		.db = "",
		.collection = "",
		.isCluster = true,
		.numActions = 1,
		.actions = (const StringView[]) {
			{ .string = "listDatabases", .length = 13 }
		}
	}
};


static const Privilege dbAdminPrivileges[] = {
	{
		.db = "admin",
		.collection = "",
		.isCluster = false,
		.numActions = 15,
		.actions = (const StringView[]) {
			{ .string = "analyze", .length = 7 },
			{ .string = "bypassDocumentValidation", .length = 24 },
			{ .string = "collMod", .length = 7 },
			{ .string = "collStats", .length = 9 },
			{ .string = "compact", .length = 7 },
			{ .string = "createCollection", .length = 16 },
			{ .string = "createIndex", .length = 11 },
			{ .string = "dbStats", .length = 7 },
			{ .string = "dropCollection", .length = 14 },
			{ .string = "dropDatabase", .length = 12 },
			{ .string = "dropIndex", .length = 9 },
			{ .string = "listCollections", .length = 15 },
			{ .string = "listIndexes", .length = 11 },
			{ .string = "reIndex", .length = 7 },
			{ .string = "validate", .length = 8 }
		}
	}
};

static const Privilege userAdminPrivileges[] = {
	{
		.db = "admin",
		.collection = "",
		.isCluster = false,
		.numActions = 8,
		.actions = (const StringView[]) {
			{ .string = "createRole", .length = 11 },
			{ .string = "createUser", .length = 11 },
			{ .string = "dropRole", .length = 8 },
			{ .string = "dropUser", .length = 8 },
			{ .string = "grantRole", .length = 9 },
			{ .string = "revokeRole", .length = 10 },
			{ .string = "viewRole", .length = 8 },
			{ .string = "viewUser", .length = 8 }
		}
	}
};

static const Privilege clusterMonitorPrivileges[] = {
	{
		.db = "",
		.collection = "",
		.isCluster = true,
		.numActions = 11,
		.actions = (const StringView[]) {
			{ .string = "connPoolStats", .length = 13 },
			{ .string = "getDefaultRWConcern", .length = 19 },
			{ .string = "getCmdLineOpts", .length = 14 },
			{ .string = "getLog", .length = 6 },
			{ .string = "getParameter", .length = 12 },
			{ .string = "getShardMap", .length = 11 },
			{ .string = "hostInfo", .length = 8 },
			{ .string = "listDatabases", .length = 13 },
			{ .string = "listSessions", .length = 12 },
			{ .string = "listShards", .length = 10 },
			{ .string = "serverStatus", .length = 12 }
		}
	},
	{
		.db = "",
		.collection = "",
		.isCluster = false,
		.numActions = 5,
		.actions = (const StringView[]) {
			{ .string = "collStats", .length = 9 },
			{ .string = "dbStats", .length = 7 },
			{ .string = "getDatabaseVersion", .length = 18 },
			{ .string = "getShardVersion", .length = 15 },
			{ .string = "indexStats", .length = 10 }
		}
	}
};

static const Privilege clusterManagerPrivileges[] = {
	{
		.db = "",
		.collection = "",
		.isCluster = true,
		.numActions = 6,
		.actions = (const StringView[]) {
			{ .string = "getClusterParameter", .length = 19 },
			{ .string = "getDefaultRWConcern", .length = 19 },
			{ .string = "listSessions", .length = 12 },
			{ .string = "listShards", .length = 10 },
			{ .string = "setChangeStreamState", .length = 20 },
			{ .string = "getChangeStreamState", .length = 20 }
		}
	},
	{
		.db = "",
		.collection = "",
		.isCluster = false,
		.numActions = 5,
		.actions = (const StringView[]) {
			{
				.string = "analyzeShardKey", .length = 15
			},
			{ .string = "enableSharding", .length = 14 },
			{ .string = "reshardCollection", .length = 17 },
			{ .string = "splitVector", .length = 11 },
			{ .string = "unshardCollection", .length = 17 }
		}
	},
};

static const Privilege hostManagerPrivileges[] = {
	{
		.db = "",
		.collection = "",
		.isCluster = true,
		.numActions = 5,
		.actions = (const StringView[]) {
			{ .string = "compact", .length = 7 },
			{ .string = "dropConnections", .length = 15 },
			{ .string = "killAnyCursor", .length = 13 },
			{ .string = "killAnySession", .length = 14 },
			{ .string = "killop", .length = 6 }
		}
	},
	{
		.db = "",
		.collection = "",
		.isCluster = false,
		.numActions = 1,
		.actions = (const StringView[]) {
			{
				.string = "killCursors", .length = 11
			}
		}
	}
};

static const Privilege dropDatabasePrivileges[] = {
	{
		.db = "",
		.collection = "",
		.isCluster = false,
		.numActions = 1,
		.actions = (const StringView[]) {
			{ .string = "dropDatabase", .length = 12 }
		}
	}
};


/*
 * Consolidates privileges for a role and
 * writes them to the provided BSON array writer.
 */
void
WriteSingleRolePrivileges(const char *roleName,
						  pgbson_array_writer *privilegesArrayWriter)
{
	if (roleName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
						errmsg("Role name cannot be NULL.")));
	}

	List *consolidatedPrivileges = NIL;

	ConsolidatePrivilegesForRole(roleName, &consolidatedPrivileges);

	WritePrivilegeListToArray(consolidatedPrivileges, privilegesArrayWriter);
	DeepFreePrivileges(consolidatedPrivileges);
}


/*
 * Consolidates privileges for all roles in the provided HTAB and
 * writes them to the provided BSON array writer.
 * The rolesTable should contain StringView entries representing role names.
 */
void
WriteMultipleRolePrivileges(HTAB *rolesTable,
							pgbson_array_writer *privilegesArrayWriter)
{
	if (rolesTable == NULL)
	{
		return;
	}

	List *consolidatedPrivileges = NIL;
	HASH_SEQ_STATUS status;
	StringView *roleEntry;

	hash_seq_init(&status, rolesTable);
	while ((roleEntry = hash_seq_search(&status)) != NULL)
	{
		/* Convert StringView to null-terminated string */
		char *roleName = palloc(roleEntry->length + 1);
		memcpy(roleName, roleEntry->string, roleEntry->length);
		roleName[roleEntry->length] = '\0';

		ConsolidatePrivilegesForRole(roleName, &consolidatedPrivileges);

		pfree(roleName);
	}

	WritePrivilegeListToArray(consolidatedPrivileges, privilegesArrayWriter);
	DeepFreePrivileges(consolidatedPrivileges);
}


/*
 * Check if the given name contains any reserved pg role name prefixes.
 */
bool
ContainsReservedPgRoleNamePrefix(const char *name)
{
	/* Split the blocked role prefix list */
	char *blockedRolePrefixList = pstrdup(BlockedRolePrefixList);
	bool containsBlockedPrefix = false;
	char *token = strtok(blockedRolePrefixList, ",");
	while (token != NULL)
	{
		if (strncmp(name, token, strlen(token)) == 0)
		{
			containsBlockedPrefix = true;
			break;
		}
		token = strtok(NULL, ",");
	}

	pfree(blockedRolePrefixList);
	return containsBlockedPrefix;
}


/*
 * Consolidates privileges for a given role name into the provided list.
 * Ignores unknown roles silently.
 */
static void
ConsolidatePrivilegesForRole(const char *roleName, List **consolidatedPrivileges)
{
	if (roleName == NULL || consolidatedPrivileges == NULL)
	{
		return;
	}

	size_t sourcePrivilegeCount;

	if (strcmp(roleName, ApiReadOnlyRole) == 0)
	{
		sourcePrivilegeCount = sizeof(readOnlyPrivileges) / sizeof(readOnlyPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, readOnlyPrivileges,
							  sourcePrivilegeCount);
	}
	else if (strcmp(roleName, ApiReadWriteRole) == 0)
	{
		sourcePrivilegeCount = sizeof(readWritePrivileges) /
							   sizeof(readWritePrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, readWritePrivileges,
							  sourcePrivilegeCount);
	}
	else if (strcmp(roleName, ApiAdminRoleV2) == 0)
	{
		sourcePrivilegeCount = sizeof(readWritePrivileges) /
							   sizeof(readWritePrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, readWritePrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(clusterManagerPrivileges) /
							   sizeof(clusterManagerPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, clusterManagerPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(clusterMonitorPrivileges) /
							   sizeof(clusterMonitorPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, clusterMonitorPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(hostManagerPrivileges) /
							   sizeof(hostManagerPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, hostManagerPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(dropDatabasePrivileges) /
							   sizeof(dropDatabasePrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, dropDatabasePrivileges,
							  sourcePrivilegeCount);
	}
	else if (strcmp(roleName, ApiUserAdminRole) == 0)
	{
		sourcePrivilegeCount = sizeof(userAdminPrivileges) /
							   sizeof(userAdminPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, userAdminPrivileges,
							  sourcePrivilegeCount);
	}
	else if (strcmp(roleName, ApiRootRole) == 0)
	{
		sourcePrivilegeCount = sizeof(readWritePrivileges) /
							   sizeof(readWritePrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, readWritePrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(dbAdminPrivileges) /
							   sizeof(dbAdminPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, dbAdminPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(userAdminPrivileges) /
							   sizeof(userAdminPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, userAdminPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(clusterMonitorPrivileges) /
							   sizeof(clusterMonitorPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, clusterMonitorPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(clusterManagerPrivileges) /
							   sizeof(clusterManagerPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, clusterManagerPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(hostManagerPrivileges) /
							   sizeof(hostManagerPrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, hostManagerPrivileges,
							  sourcePrivilegeCount);

		sourcePrivilegeCount = sizeof(dropDatabasePrivileges) /
							   sizeof(dropDatabasePrivileges[0]);
		ConsolidatePrivileges(consolidatedPrivileges, dropDatabasePrivileges,
							  sourcePrivilegeCount);
	}

	/* Unknown roles are silently ignored */
}


/*
 * Takes a list of source privileges and consolidates them into the
 * provided list of consolidated privileges, merging any duplicate privileges and combining their actions.
 */
static void
ConsolidatePrivileges(List **consolidatedPrivileges,
					  const Privilege *sourcePrivileges,
					  size_t sourcePrivilegeCount)
{
	if (sourcePrivileges == NULL)
	{
		return;
	}

	for (size_t i = 0; i < sourcePrivilegeCount; i++)
	{
		ConsolidatePrivilege(consolidatedPrivileges, &sourcePrivileges[i]);
	}
}


/*
 * Consolidates a single source privilege into the list of consolidated privileges.
 * If a privilege with the same resource target already exists, its actions are merged with the source privilege.
 * Otherwise, a new privilege is created and added to the list.
 */
static void
ConsolidatePrivilege(List **consolidatedPrivileges, const Privilege *sourcePrivilege)
{
	if (sourcePrivilege == NULL || sourcePrivilege->numActions == 0)
	{
		return;
	}

	ListCell *privilege;
	ConsolidatedPrivilege *existingPrivilege = NULL;

	foreach(privilege, *consolidatedPrivileges)
	{
		ConsolidatedPrivilege *currentPrivilege =
			(ConsolidatedPrivilege *) lfirst(privilege);

		if (ComparePrivileges(currentPrivilege, sourcePrivilege))
		{
			existingPrivilege = currentPrivilege;
			break;
		}
	}

	if (existingPrivilege != NULL)
	{
		for (size_t i = 0; i < sourcePrivilege->numActions; i++)
		{
			bool actionFound;

			/* The consolidated privilege does not free the actual char* in the action HTAB;
			 * therefore it is safe to pass actions[i]. */
			hash_search(existingPrivilege->actions,
						&sourcePrivilege->actions[i], HASH_ENTER,
						&actionFound);
		}
	}
	else
	{
		ConsolidatedPrivilege *newPrivilege = palloc0(
			sizeof(ConsolidatedPrivilege));
		newPrivilege->isCluster = sourcePrivilege->isCluster;
		newPrivilege->db = pstrdup(sourcePrivilege->db);
		newPrivilege->collection = pstrdup(sourcePrivilege->collection);
		newPrivilege->actions = CreateStringViewHashSet();

		for (size_t i = 0; i < sourcePrivilege->numActions; i++)
		{
			bool actionFound;

			hash_search(newPrivilege->actions,
						&sourcePrivilege->actions[i],
						HASH_ENTER, &actionFound);
		}

		*consolidatedPrivileges = lappend(*consolidatedPrivileges, newPrivilege);
	}
}


/*
 * Checks if two privileges have the same resource (same cluster status and db/collection).
 */
static bool
ComparePrivileges(const ConsolidatedPrivilege *privilege1,
				  const Privilege *privilege2)
{
	if (privilege1->isCluster != privilege2->isCluster)
	{
		return false;
	}

	if (privilege1->isCluster)
	{
		return true;
	}

	return (strcmp(privilege1->db, privilege2->db) == 0 &&
			strcmp(privilege1->collection, privilege2->collection) == 0);
}


/*
 * Helper function to write a single privilege document.
 */
static void
WriteSinglePrivilegeDocument(const ConsolidatedPrivilege *privilege,
							 pgbson_array_writer *privilegesArrayWriter)
{
	pgbson_writer privilegeWriter;
	PgbsonArrayWriterStartDocument(privilegesArrayWriter, &privilegeWriter);

	pgbson_writer resourceWriter;
	PgbsonWriterStartDocument(&privilegeWriter, "resource", 8,
							  &resourceWriter);
	if (privilege->isCluster)
	{
		PgbsonWriterAppendBool(&resourceWriter, "cluster", 7,
							   true);
	}
	else
	{
		PgbsonWriterAppendUtf8(&resourceWriter, "db", 2,
							   privilege->db);
		PgbsonWriterAppendUtf8(&resourceWriter, "collection", 10,
							   privilege->collection);
	}
	PgbsonWriterEndDocument(&privilegeWriter, &resourceWriter);

	pgbson_array_writer actionsArrayWriter;
	PgbsonWriterStartArray(&privilegeWriter, "actions", 7,
						   &actionsArrayWriter);

	if (privilege->actions != NULL)
	{
		HASH_SEQ_STATUS status;
		StringView *privilegeEntry;
		List *actionList = NIL;

		hash_seq_init(&status, privilege->actions);
		while ((privilegeEntry = hash_seq_search(&status)) != NULL)
		{
			char *actionString = palloc(privilegeEntry->length + 1);
			memcpy(actionString, privilegeEntry->string, privilegeEntry->length);
			actionString[privilegeEntry->length] = '\0';
			actionList = lappend(actionList, actionString);
		}

		if (actionList != NIL)
		{
			SortStringList(actionList);
			ListCell *cell;
			foreach(cell, actionList)
			{
				PgbsonArrayWriterWriteUtf8(&actionsArrayWriter, (const char *) lfirst(
											   cell));
			}
			list_free_deep(actionList);
		}
	}

	PgbsonWriterEndArray(&privilegeWriter, &actionsArrayWriter);

	PgbsonArrayWriterEndDocument(privilegesArrayWriter, &privilegeWriter);
}


/*
 * Writes the consolidated privileges list to a BSON array.
 */
static void
WritePrivilegeListToArray(List *consolidatedPrivileges,
						  pgbson_array_writer *privilegesArrayWriter)
{
	ListCell *privilege;
	foreach(privilege, consolidatedPrivileges)
	{
		ConsolidatedPrivilege *currentPrivilege =
			(ConsolidatedPrivilege *) lfirst(privilege);
		WriteSinglePrivilegeDocument(currentPrivilege, privilegesArrayWriter);
	}
}


/*
 * Frees all memory allocated for the consolidated privileges list,
 * including strings and hash table entries.
 */
static void
DeepFreePrivileges(List *consolidatedPrivileges)
{
	if (consolidatedPrivileges == NIL)
	{
		return;
	}

	ListCell *privilege;
	foreach(privilege, consolidatedPrivileges)
	{
		ConsolidatedPrivilege *currentPrivilege =
			(ConsolidatedPrivilege *) lfirst(privilege);

		if (currentPrivilege->db)
		{
			pfree((char *) currentPrivilege->db);
		}

		if (currentPrivilege->collection)
		{
			pfree((char *) currentPrivilege->collection);
		}

		if (currentPrivilege->actions)
		{
			hash_destroy(currentPrivilege->actions);
		}
	}

	list_free_deep(consolidatedPrivileges);
}


List *
ConvertUserOrRoleNamesDatumToList(Datum *nameDatums, int namesCount)
{
	List *namesList = NIL;

	for (int i = 0; i < namesCount; i++)
	{
		text *nameText = DatumGetTextP(nameDatums[i]);
		const char *currentName = text_to_cstring(nameText);

		if (currentName != NULL && strlen(currentName) > 0)
		{
			namesList = lappend(namesList,
								pstrdup(currentName));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
							errmsg(
								"Name array datum is NULL or empty.")));
		}
	}

	return namesList;
}
