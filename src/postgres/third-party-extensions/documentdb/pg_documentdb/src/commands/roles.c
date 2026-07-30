/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/roles.c
 *
 * Implementation of role CRUD functions.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "access/transam.h"
#include "utils/documentdb_errors.h"
#include "utils/query_utils.h"
#include "commands/commands_common.h"
#include "commands/parse_error.h"
#include "utils/feature_counter.h"
#include "metadata/metadata_cache.h"
#include "api_hooks_def.h"
#include "api_hooks.h"
#include "utils/list_utils.h"
#include "roles.h"
#include "utils/elog.h"
#include "utils/array.h"
#include "utils/hashset_utils.h"
#include "utils/role_utils.h"

/* GUC to enable user crud operations */
extern bool EnableRoleCrud;

/* GUC that controls whether the DB admin check is enabled */
extern bool EnableRolesAdminDBCheck;

PG_FUNCTION_INFO_V1(command_create_role);
PG_FUNCTION_INFO_V1(command_drop_role);
PG_FUNCTION_INFO_V1(command_roles_info);
PG_FUNCTION_INFO_V1(command_update_role);

static void ParseCreateRoleSpec(pgbson *createRoleBson, CreateRoleSpec *createRoleSpec);
static void ParseRolesArray(bson_iter_t *rolesIter, CreateRoleSpec *createRoleSpec);
static void GrantInheritedRoles(const CreateRoleSpec *createRoleSpec);
static void ParseDropRoleSpec(pgbson *dropRoleBson, DropRoleSpec *dropRoleSpec);
static void ParseRolesInfoSpec(pgbson *rolesInfoBson, RolesInfoSpec *rolesInfoSpec);
static void ParseRoleDefinition(bson_iter_t *iter, RolesInfoSpec *rolesInfoSpec);
static void ParseRoleDocument(bson_iter_t *rolesArrayIter, RolesInfoSpec *rolesInfoSpec);
static void ProcessAllRoles(pgbson_array_writer *rolesArrayWriter, RolesInfoSpec
							rolesInfoSpec);
static void ProcessSpecificRoles(pgbson_array_writer *rolesArrayWriter, RolesInfoSpec
								 rolesInfoSpec);
static void WriteRoleResponse(const char *roleName,
							  pgbson_array_writer *rolesArrayWriter,
							  RolesInfoSpec rolesInfoSpec);
static List * FetchDirectParentRoleNames(const char *roleName);

/*
 * Parses a createRole spec, executes the createRole command, and returns the result.
 */
Datum
command_create_role(PG_FUNCTION_ARGS)
{
	pgbson *createRoleSpec = PG_GETARG_PGBSON(0);

	Datum response = create_role(createRoleSpec);

	PG_RETURN_DATUM(response);
}


/*
 * Implements dropRole command.
 */
Datum
command_drop_role(PG_FUNCTION_ARGS)
{
	pgbson *dropRoleSpec = PG_GETARG_PGBSON(0);

	Datum response = drop_role(dropRoleSpec);

	PG_RETURN_DATUM(response);
}


/*
 * Implements rolesInfo command, which will be implemented in the future.
 */
Datum
command_roles_info(PG_FUNCTION_ARGS)
{
	pgbson *rolesInfoSpec = PG_GETARG_PGBSON(0);

	Datum response = roles_info(rolesInfoSpec);

	PG_RETURN_DATUM(response);
}


/*
 * Implements updateRole command, which will be implemented in the future.
 */
Datum
command_update_role(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
					errmsg("UpdateRole command is not supported in preview."),
					errdetail_log("UpdateRole command is not supported in preview.")));
}


/*
 * create_role implements the core logic for createRole command
 */
Datum
create_role(pgbson *createRoleBson)
{
	if (!EnableRoleCrud)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("The CreateRole command is currently unsupported."),
						errdetail_log(
							"The CreateRole command is currently unsupported.")));
	}

	ReportFeatureUsage(FEATURE_ROLE_CREATE);

	if (!IsMetadataCoordinator())
	{
		StringInfo createRoleQuery = makeStringInfo();
		appendStringInfo(createRoleQuery,
						 "SELECT %s.create_role(%s::%s.bson)",
						 ApiSchemaNameV2,
						 quote_literal_cstr(PgbsonToHexadecimalString(createRoleBson)),
						 CoreSchemaNameV2);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			createRoleQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Create role operation failed: %s",
								text_to_cstring(result.response)),
							errdetail_log(
								"Create role operation failed: %s",
								text_to_cstring(result.response))));
		}

		pgbson_writer finalWriter;
		PgbsonWriterInit(&finalWriter);
		PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
		return PointerGetDatum(PgbsonWriterGetPgbson(&finalWriter));
	}

	CreateRoleSpec createRoleSpec = { NULL, NIL };
	ParseCreateRoleSpec(createRoleBson, &createRoleSpec);

	/* Validate that at least one inherited role is specified */
	if (list_length(createRoleSpec.parentRoles) == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"At least one inherited role must be specified in 'roles' array.")));
	}

	/* Create the specified role in the database */
	StringInfo createRoleInfo = makeStringInfo();
	appendStringInfo(createRoleInfo, "CREATE ROLE %s", quote_identifier(
						 createRoleSpec.roleName));

	bool readOnly = false;
	bool isNull = false;
	ExtensionExecuteQueryViaSPI(createRoleInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	/* Grant inherited roles to the new role */
	GrantInheritedRoles(&createRoleSpec);

	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
	return PointerGetDatum(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * ParseCreateRoleSpec parses the createRole command parameters
 */
static void
ParseCreateRoleSpec(pgbson *createRoleBson, CreateRoleSpec *createRoleSpec)
{
	bson_iter_t createRoleIter;
	PgbsonInitIterator(createRoleBson, &createRoleIter);
	bool dbFound = false;
	while (bson_iter_next(&createRoleIter))
	{
		const char *key = bson_iter_key(&createRoleIter);

		if (strcmp(key, "createRole") == 0)
		{
			EnsureTopLevelFieldType(key, &createRoleIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			createRoleSpec->roleName = bson_iter_utf8(&createRoleIter, &strLength);

			if (strLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"The 'createRole' field must not be left empty.")));
			}

			if (ContainsReservedPgRoleNamePrefix(createRoleSpec->roleName))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"Role name '%s' is reserved and can't be used as a custom role name.",
									createRoleSpec->roleName)));
			}
		}
		else if (strcmp(key, "roles") == 0)
		{
			ParseRolesArray(&createRoleIter, createRoleSpec);
		}
		else if (strcmp(key, "$db") == 0 && EnableRolesAdminDBCheck)
		{
			EnsureTopLevelFieldType(key, &createRoleIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			const char *dbName = bson_iter_utf8(&createRoleIter, &strLength);

			dbFound = true;
			if (strcmp(dbName, "admin") != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"CreateRole must be called from 'admin' database.")));
			}
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			continue;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("The specified field '%s' is not supported.", key)));
		}
	}

	if (!dbFound && EnableRolesAdminDBCheck)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("The required $db property is missing.")));
	}

	if (createRoleSpec->roleName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("'createRole' is a required field.")));
	}
}


/*
 * ParseRolesArray parses the "roles" array from the createRole command.
 * Extracts inherited built-in role names.
 */
static void
ParseRolesArray(bson_iter_t *rolesIter, CreateRoleSpec *createRoleSpec)
{
	if (bson_iter_type(rolesIter) != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Expected 'array' type for 'roles' parameter but found '%s' type",
							BsonTypeName(bson_iter_type(rolesIter)))));
	}

	bson_iter_t rolesArrayIter;
	bson_iter_recurse(rolesIter, &rolesArrayIter);

	while (bson_iter_next(&rolesArrayIter))
	{
		if (bson_iter_type(&rolesArrayIter) != BSON_TYPE_UTF8)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg(
								"Invalid inherited from role name provided.")));
		}

		uint32_t roleNameLength = 0;
		const char *inheritedBuiltInRole = bson_iter_utf8(&rolesArrayIter,
														  &roleNameLength);

		if (roleNameLength > 0)
		{
			createRoleSpec->parentRoles = lappend(
				createRoleSpec->parentRoles,
				pstrdup(inheritedBuiltInRole));
		}
	}
}


/*
 * GrantInheritedRoles grants the inherited built-in roles to the new role.
 * Validates that each role is a supported built-in role before granting.
 */
static void
GrantInheritedRoles(const CreateRoleSpec *createRoleSpec)
{
	bool readOnly = false;
	bool isNull = false;

	ListCell *currentRole;
	foreach(currentRole, createRoleSpec->parentRoles)
	{
		const char *inheritedRole = (const char *) lfirst(currentRole);

		if (!IS_BUILTIN_ROLE(inheritedRole))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_ROLENOTFOUND),
							errmsg("Role '%s' not supported.",
								   inheritedRole)));
		}

		StringInfo grantRoleInfo = makeStringInfo();
		appendStringInfo(grantRoleInfo, "GRANT %s TO %s",
						 quote_identifier(inheritedRole),
						 quote_identifier(createRoleSpec->roleName));

		ExtensionExecuteQueryViaSPI(grantRoleInfo->data, readOnly, SPI_OK_UTILITY,
									&isNull);
	}
}


/*
 * update_role implements the core logic for updateRole command
 * Currently not supported.
 */
Datum
update_role(pgbson *updateRoleBson)
{
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
					errmsg("UpdateRole command is not supported in preview."),
					errdetail_log("UpdateRole command is not supported in preview.")));
}


/*
 * drop_role implements the core logic for dropRole command
 */
Datum
drop_role(pgbson *dropRoleBson)
{
	if (!EnableRoleCrud)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("DropRole command is not supported."),
						errdetail_log("DropRole command is not supported.")));
	}

	if (!IsMetadataCoordinator())
	{
		StringInfo dropRoleQuery = makeStringInfo();
		appendStringInfo(dropRoleQuery,
						 "SELECT %s.drop_role(%s::%s.bson)",
						 ApiSchemaNameV2,
						 quote_literal_cstr(PgbsonToHexadecimalString(dropRoleBson)),
						 CoreSchemaNameV2);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			dropRoleQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Drop role operation failed: %s",
								text_to_cstring(result.response)),
							errdetail_log(
								"Drop role operation failed: %s",
								text_to_cstring(result.response))));
		}

		pgbson_writer finalWriter;
		PgbsonWriterInit(&finalWriter);
		PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
		return PointerGetDatum(PgbsonWriterGetPgbson(&finalWriter));
	}

	DropRoleSpec dropRoleSpec = { NULL };
	ParseDropRoleSpec(dropRoleBson, &dropRoleSpec);

	StringInfo dropUserInfo = makeStringInfo();
	appendStringInfo(dropUserInfo, "DROP ROLE %s;", quote_identifier(
						 dropRoleSpec.roleName));

	bool readOnly = false;
	bool isNull = false;
	ExtensionExecuteQueryViaSPI(dropUserInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
	return PointerGetDatum(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * ParseDropRoleSpec parses the dropRole command parameters
 */
static void
ParseDropRoleSpec(pgbson *dropRoleBson, DropRoleSpec *dropRoleSpec)
{
	bson_iter_t dropRoleIter;
	PgbsonInitIterator(dropRoleBson, &dropRoleIter);
	bool dbFound = false;
	while (bson_iter_next(&dropRoleIter))
	{
		const char *key = bson_iter_key(&dropRoleIter);

		if (strcmp(key, "dropRole") == 0)
		{
			EnsureTopLevelFieldType(key, &dropRoleIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			const char *roleNameValue = bson_iter_utf8(&dropRoleIter, &strLength);

			if (strLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("'dropRole' cannot be empty.")));
			}

			if (IS_BUILTIN_ROLE(roleNameValue) || IS_SYSTEM_LOGIN_ROLE(roleNameValue))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"Cannot drop built-in role '%s'.",
									roleNameValue)));
			}

			dropRoleSpec->roleName = pstrdup(roleNameValue);
		}
		else if (strcmp(key, "$db") == 0 && EnableRolesAdminDBCheck)
		{
			EnsureTopLevelFieldType(key, &dropRoleIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			const char *dbName = bson_iter_utf8(&dropRoleIter, &strLength);

			dbFound = true;
			if (strcmp(dbName, "admin") != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"DropRole must be called from 'admin' database.")));
			}
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			continue;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unsupported field specified: '%s'.", key)));
		}
	}

	if (!dbFound && EnableRolesAdminDBCheck)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("The required $db property is missing.")));
	}

	if (dropRoleSpec->roleName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("'dropRole' is a required field.")));
	}
}


/*
 * roles_info implements the core logic for rolesInfo command
 */
Datum
roles_info(pgbson *rolesInfoBson)
{
	if (!EnableRoleCrud)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("RolesInfo command is not supported."),
						errdetail_log("RolesInfo command is not supported.")));
	}

	if (!IsMetadataCoordinator())
	{
		StringInfo rolesInfoQuery = makeStringInfo();
		appendStringInfo(rolesInfoQuery,
						 "SELECT %s.roles_info(%s::%s.bson)",
						 ApiSchemaNameV2,
						 quote_literal_cstr(PgbsonToHexadecimalString(rolesInfoBson)),
						 CoreSchemaNameV2);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			rolesInfoQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Roles info operation failed: %s",
								text_to_cstring(result.response)),
							errdetail_log(
								"Roles info operation failed: %s",
								text_to_cstring(result.response))));
		}

		pgbson_writer finalWriter;
		PgbsonWriterInit(&finalWriter);
		PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
		return PointerGetDatum(PgbsonWriterGetPgbson(&finalWriter));
	}

	RolesInfoSpec rolesInfoSpec = {
		.roleNames = NIL,
		.showAllRoles = false,
		.showBuiltInRoles = false,
		.showPrivileges = false
	};
	ParseRolesInfoSpec(rolesInfoBson, &rolesInfoSpec);

	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);

	pgbson_array_writer rolesArrayWriter;
	PgbsonWriterStartArray(&finalWriter, "roles", 5, &rolesArrayWriter);

	if (rolesInfoSpec.showAllRoles)
	{
		ProcessAllRoles(&rolesArrayWriter, rolesInfoSpec);
	}
	else
	{
		ProcessSpecificRoles(&rolesArrayWriter, rolesInfoSpec);
	}

	/* This array is populated in ParseRolesInfoSpec, and freed here */
	if (rolesInfoSpec.roleNames != NIL)
	{
		list_free_deep(rolesInfoSpec.roleNames);
	}

	PgbsonWriterEndArray(&finalWriter, &rolesArrayWriter);
	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);

	return PointerGetDatum(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * ParseRolesInfoSpec parses the rolesInfo command parameters
 */
static void
ParseRolesInfoSpec(pgbson *rolesInfoBson, RolesInfoSpec *rolesInfoSpec)
{
	bson_iter_t rolesInfoIter;
	PgbsonInitIterator(rolesInfoBson, &rolesInfoIter);

	rolesInfoSpec->roleNames = NIL;
	rolesInfoSpec->showAllRoles = false;
	rolesInfoSpec->showBuiltInRoles = false;
	rolesInfoSpec->showPrivileges = false;
	bool rolesInfoFound = false;
	bool dbFound = false;
	while (bson_iter_next(&rolesInfoIter))
	{
		const char *key = bson_iter_key(&rolesInfoIter);

		if (strcmp(key, "rolesInfo") == 0)
		{
			rolesInfoFound = true;
			if (bson_iter_type(&rolesInfoIter) == BSON_TYPE_INT32)
			{
				int32_t value = bson_iter_int32(&rolesInfoIter);
				if (value == 1)
				{
					rolesInfoSpec->showAllRoles = true;
				}
				else
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
									errmsg(
										"'rolesInfo' must be 1, a string, a document, or an array.")));
				}
			}
			else if (bson_iter_type(&rolesInfoIter) == BSON_TYPE_ARRAY)
			{
				bson_iter_t rolesArrayIter;
				bson_iter_recurse(&rolesInfoIter, &rolesArrayIter);

				while (bson_iter_next(&rolesArrayIter))
				{
					ParseRoleDefinition(&rolesArrayIter, rolesInfoSpec);
				}
			}
			else
			{
				ParseRoleDefinition(&rolesInfoIter, rolesInfoSpec);
			}
		}
		else if (strcmp(key, "showBuiltInRoles") == 0)
		{
			if (BSON_ITER_HOLDS_BOOL(&rolesInfoIter))
			{
				rolesInfoSpec->showBuiltInRoles = bson_iter_as_bool(&rolesInfoIter);
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"'showBuiltInRoles' must be a boolean value")));
			}
		}
		else if (strcmp(key, "showPrivileges") == 0)
		{
			if (BSON_ITER_HOLDS_BOOL(&rolesInfoIter))
			{
				rolesInfoSpec->showPrivileges = bson_iter_as_bool(&rolesInfoIter);
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"'showPrivileges' must be a boolean value")));
			}
		}
		else if (strcmp(key, "$db") == 0 && EnableRolesAdminDBCheck)
		{
			EnsureTopLevelFieldType(key, &rolesInfoIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			const char *dbName = bson_iter_utf8(&rolesInfoIter, &strLength);

			dbFound = true;
			if (strcmp(dbName, "admin") != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"RolesInfo must be called from 'admin' database.")));
			}
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			continue;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unsupported field specified: '%s'.", key)));
		}
	}

	if (!dbFound && EnableRolesAdminDBCheck)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("The required $db property is missing.")));
	}

	if (!rolesInfoFound)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("'rolesInfo' is a required field.")));
	}
}


/*
 * Helper function to parse a role document from an array element or single document
 */
static void
ParseRoleDocument(bson_iter_t *rolesArrayIter, RolesInfoSpec *rolesInfoSpec)
{
	bson_iter_t roleDocIter;
	bson_iter_recurse(rolesArrayIter, &roleDocIter);

	const char *roleName = NULL;
	uint32_t roleNameLength = 0;
	const char *dbName = NULL;
	uint32_t dbNameLength = 0;

	while (bson_iter_next(&roleDocIter))
	{
		const char *roleKey = bson_iter_key(&roleDocIter);

		if (strcmp(roleKey, "role") == 0)
		{
			if (bson_iter_type(&roleDocIter) != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("'role' field must be a string.")));
			}

			roleName = bson_iter_utf8(&roleDocIter, &roleNameLength);
		}

		/* db is required as part of every role document. */
		else if (strcmp(roleKey, "db") == 0)
		{
			if (bson_iter_type(&roleDocIter) != BSON_TYPE_UTF8)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("'db' field must be a string.")));
			}

			dbName = bson_iter_utf8(&roleDocIter, &dbNameLength);

			if (strcmp(dbName, "admin") != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"Unsupported value specified for db. Only 'admin' is allowed.")));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unknown property '%s' in role document.", roleKey)));
		}
	}

	if (roleName == NULL || dbName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("'role' and 'db' are required fields.")));
	}

	/* Only add role to the list if both role name and db name have valid lengths */
	if (roleNameLength > 0 && dbNameLength > 0)
	{
		rolesInfoSpec->roleNames = lappend(rolesInfoSpec->roleNames, pstrdup(roleName));
	}
}


/*
 * Helper function to parse a role definition (string or document)
 */
static void
ParseRoleDefinition(bson_iter_t *iter, RolesInfoSpec *rolesInfoSpec)
{
	if (bson_iter_type(iter) == BSON_TYPE_UTF8)
	{
		uint32_t roleNameLength = 0;
		const char *roleName = bson_iter_utf8(iter, &roleNameLength);

		/* If the string is empty, we will not add it to the list of roles to fetched */
		if (roleNameLength > 0)
		{
			rolesInfoSpec->roleNames = lappend(rolesInfoSpec->roleNames, pstrdup(
												   roleName));
		}
	}
	else if (bson_iter_type(iter) == BSON_TYPE_DOCUMENT)
	{
		ParseRoleDocument(iter, rolesInfoSpec);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"'rolesInfo' must be 1, a string, a document, or an array.")));
	}
}


/*
 * ProcessAllRoles handles the case when showAllRoles is true.
 * It retrieves all roles from pg_roles table and writes their details to the response array.
 */
static void
ProcessAllRoles(pgbson_array_writer *rolesArrayWriter, RolesInfoSpec rolesInfoSpec)
{
	/*
	 * Postgres reserves system objects which have OID less than FirstNormalObjectId.
	 * Also in Postgres, user is stored as a role in the pg_roles table, which is basically a role that can also login, so they are excluded.
	 * Lastly, DocumentDB sets certain pre-defined role(s) with login privilege for the background jobs, so we cannot exclude them.
	 */
	const char *cmdStr = FormatSqlQuery(
		"SELECT ARRAY_AGG(CASE WHEN rolname = '%s' THEN '%s' ELSE rolname::text END ORDER BY rolname) "
		"FROM pg_roles "
		"WHERE oid >= %d AND (NOT rolcanlogin OR rolname = '%s');",
		ApiRootInternalRole, ApiRootRole,
		FirstNormalObjectId, ApiAdminRole);

	bool readOnly = true;
	bool isNull = false;
	Datum allRoleNamesDatum = ExtensionExecuteQueryViaSPI(cmdStr, readOnly, SPI_OK_SELECT,
														  &isNull);

	if (isNull)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Failed to retrieve roles from pg_roles table.")));
	}

	ArrayType *roleNameArray = DatumGetArrayTypeP(allRoleNamesDatum);
	Oid arrayElementType = ARR_ELEMTYPE(roleNameArray);
	int elementLength = -1;
	bool arrayByVal = false;

	Datum *roleNameDatums;
	bool *roleNameIsNullMarker;
	int roleCount;

	deconstruct_array(roleNameArray, arrayElementType, elementLength, arrayByVal,
					  TYPALIGN_INT,
					  &roleNameDatums, &roleNameIsNullMarker, &roleCount);

	for (int i = 0; i < roleCount; i++)
	{
		if (roleNameIsNullMarker[i])
		{
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
							errmsg(
								"Encountered NULL roleDatum while processing role documents array.")));
		}

		text *roleText = DatumGetTextP(roleNameDatums[i]);
		if (roleText == NULL || VARSIZE_ANY_EXHDR(roleText) == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
							errmsg(
								"Encountered NULL or empty roleText while processing role documents array.")));
		}

		const char *roleName = text_to_cstring(roleText);

		if (roleName == NULL || strlen(roleName) == 0)
		{
			ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
							errmsg(
								"roleName extracted from pg_roles is NULL or empty.")));
		}

		/* Exclude built-in roles if the request doesn't demand them */
		if (IS_SYSTEM_LOGIN_ROLE(roleName) ||
			(IS_BUILTIN_ROLE(roleName) && !rolesInfoSpec.showBuiltInRoles))
		{
			continue;
		}

		WriteRoleResponse(roleName, rolesArrayWriter,
						  rolesInfoSpec);
	}
}


/*
 * ProcessSpecificRoles handles the case when specific role names are requested.
 * It retrieves each specified role from pg_roles table and writes their details to the response array.
 */
static void
ProcessSpecificRoles(pgbson_array_writer *rolesArrayWriter, RolesInfoSpec rolesInfoSpec)
{
	ListCell *currentRoleName;
	foreach(currentRoleName, rolesInfoSpec.roleNames)
	{
		const char *requestedRoleName = (const char *) lfirst(currentRoleName);

		if (strcmp(requestedRoleName, ApiRootRole) == 0)
		{
			requestedRoleName = ApiRootInternalRole;
		}

		const char *cmdStr = FormatSqlQuery(
			"SELECT rolname "
			"FROM pg_roles "
			"WHERE oid >= %d AND (NOT rolcanlogin OR rolname = '%s') AND rolname = '%s';",
			FirstNormalObjectId, ApiAdminRole, requestedRoleName);

		bool readOnly = true;
		bool isNull = false;
		ExtensionExecuteQueryViaSPI(cmdStr, readOnly, SPI_OK_SELECT, &isNull);

		/* If the role is not found, do not fail the request */
		if (!isNull)
		{
			WriteRoleResponse(requestedRoleName, rolesArrayWriter,
							  rolesInfoSpec);
		}
	}
}


/*
 * Primitive type properties include _id, role, db, isBuiltin.
 * privileges: supported privilege actions of this role if defined.
 * roles property: 1st level directly inherited roles if defined.
 * inheritedRoles: all recursively inherited roles if defined (not yet supported).
 * inheritedPrivileges: consolidated privileges of current role and all recursively inherited roles if defined (not yet supported).
 */
static void
WriteRoleResponse(const char *roleName,
				  pgbson_array_writer *rolesArrayWriter,
				  RolesInfoSpec rolesInfoSpec)
{
	pgbson_writer roleDocumentWriter;
	PgbsonArrayWriterStartDocument(rolesArrayWriter, &roleDocumentWriter);

	char *roleId = psprintf("admin.%s", roleName);
	PgbsonWriterAppendUtf8(&roleDocumentWriter, "_id", 3, roleId);
	pfree(roleId);

	PgbsonWriterAppendUtf8(&roleDocumentWriter, "role", 4, roleName);
	PgbsonWriterAppendUtf8(&roleDocumentWriter, "db", 2, "admin");
	PgbsonWriterAppendBool(&roleDocumentWriter, "isBuiltIn", 9,
						   IS_BUILTIN_ROLE(roleName));

	/* Write privileges */
	if (rolesInfoSpec.showPrivileges)
	{
		pgbson_array_writer privilegesArrayWriter;
		PgbsonWriterStartArray(&roleDocumentWriter, "privileges", 10,
							   &privilegesArrayWriter);
		WriteSingleRolePrivileges(roleName, &privilegesArrayWriter);
		PgbsonWriterEndArray(&roleDocumentWriter, &privilegesArrayWriter);
	}

	/* Write roles */
	List *parentRoles = FetchDirectParentRoleNames(roleName);
	pgbson_array_writer parentRolesArrayWriter;
	PgbsonWriterStartArray(&roleDocumentWriter, "roles", 5, &parentRolesArrayWriter);
	ListCell *roleCell;
	foreach(roleCell, parentRoles)
	{
		const char *parentRoleName = (const char *) lfirst(roleCell);
		pgbson_writer parentRoleDocWriter;
		PgbsonArrayWriterStartDocument(&parentRolesArrayWriter,
									   &parentRoleDocWriter);
		PgbsonWriterAppendUtf8(&parentRoleDocWriter, "role", 4, parentRoleName);
		PgbsonWriterAppendUtf8(&parentRoleDocWriter, "db", 2, "admin");
		PgbsonArrayWriterEndDocument(&parentRolesArrayWriter, &parentRoleDocWriter);
	}
	PgbsonWriterEndArray(&roleDocumentWriter, &parentRolesArrayWriter);
	PgbsonArrayWriterEndDocument(rolesArrayWriter, &roleDocumentWriter);

	if (parentRoles != NIL)
	{
		list_free_deep(parentRoles);
	}
}


static List *
FetchDirectParentRoleNames(const char *roleName)
{
	const char *requestedRoleName = roleName;
	if (strcmp(roleName, ApiRootRole) == 0)
	{
		requestedRoleName = ApiRootInternalRole;
	}

	/*
	 * Even if the user has given us the role name, we still want to prevent customers fetching roles that are not allowed to be fetched.
	 * As such, we add the same filtering condition as the above PG queries.
	 */
	const char *cmdStr = FormatSqlQuery(
		"WITH parent AS ("
		"  SELECT DISTINCT parent.rolname::text AS parent_role "
		"  FROM pg_roles child "
		"  JOIN pg_auth_members am ON child.oid = am.member "
		"  JOIN pg_roles parent ON am.roleid = parent.oid "
		"  WHERE child.oid >= %d AND (NOT child.rolcanlogin OR child.rolname = '%s') AND child.rolname = '%s' "
		") "
		"SELECT ARRAY_AGG(parent_role ORDER BY parent_role) "
		"FROM parent;",
		FirstNormalObjectId, ApiAdminRole, requestedRoleName);

	bool readOnly = true;
	bool isNull = false;
	Datum parentRolesDatum = ExtensionExecuteQueryViaSPI(cmdStr, readOnly,
														 SPI_OK_SELECT, &isNull);

	List *parentRolesList = NIL;

	if (!isNull)
	{
		ArrayType *parentRolesArray = DatumGetArrayTypeP(parentRolesDatum);
		Datum *parentRolesDatums;
		bool *parentRolesIsNullMarker;
		int parentRolesCount;

		bool arrayByVal = false;
		int elementLength = -1;
		Oid arrayElementType = ARR_ELEMTYPE(parentRolesArray);
		deconstruct_array(parentRolesArray,
						  arrayElementType, elementLength, arrayByVal,
						  TYPALIGN_INT, &parentRolesDatums,
						  &parentRolesIsNullMarker,
						  &parentRolesCount);

		parentRolesList = ConvertUserOrRoleNamesDatumToList(parentRolesDatums,
															parentRolesCount);
	}

	return parentRolesList;
}
