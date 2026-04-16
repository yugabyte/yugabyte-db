/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/users.c
 *
 * Implementation of user CRUD functions.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "utils/documentdb_errors.h"
#include "utils/query_utils.h"
#include "utils/documentdb_errors.h"
#include "commands/commands_common.h"
#include "commands/parse_error.h"
#include "utils/feature_counter.h"
#include "libpq/scram.h"
#include "metadata/metadata_cache.h"
#include <common/saslprep.h>
#include <common/scram-common.h>

#define SCRAM_MAX_SALT_LEN 64

enum DocumentDB_Roles
{
	DocumentDB_Role_Read_AnyDatabase = 0x1,
	DocumentDB_Role_ReadWrite_AnyDatabase = 0x2,
	DocumentDB_Role_Cluster_Admin = 0x4,
};


typedef struct
{
	/* "createUser" field */
	const char *createUser;

	/* "pwd" field */
	const char *pwd;

	/* "roles" field */
	bson_value_t roles;

	/* pgRole the passed in role maps to */
	char *pgRole;
} CreateUserSpec;

typedef struct
{
	/* "updateUser" field */
	const char *updateUser;

	/* "pwd" field */
	const char *pwd;
} UpdateUserSpec;

/* GUC to enable user crud operations */
extern bool EnableUserCrud;

/* GUC that controls the default salt length*/
extern int ScramDefaultSaltLen;

/* GUC that controls the max number of users allowed*/
extern int MaxUserLimit;

/* GUC that controls the blocked role prefix list*/
extern char *BlockedRolePrefixList;

PG_FUNCTION_INFO_V1(documentdb_extension_create_user);
PG_FUNCTION_INFO_V1(documentdb_extension_drop_user);
PG_FUNCTION_INFO_V1(documentdb_extension_update_user);
PG_FUNCTION_INFO_V1(documentdb_extension_get_users);

static CreateUserSpec * ParseCreateUserSpec(pgbson *createUserSpec);
static char * ValidateAndObtainUserRole(const bson_value_t *rolesDocument);
static char * ParseDropUserSpec(pgbson *dropSpec);
static UpdateUserSpec * ParseUpdateUserSpec(pgbson *updateSpec);
static char * ParseGetUserSpec(pgbson *getSpec);
static char * PrehashPassword(const char *password);
static bool IsUserNameInvalid(const char *userName);

/*
 * documentdb_extension_create_user implements the
 * core logic to create a user
 */
Datum
documentdb_extension_create_user(PG_FUNCTION_ARGS)
{
	if (!EnableUserCrud)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("CreateUser command is not supported"),
						errdetail_log("CreateUser command is not supported")));
	}

	ReportFeatureUsage(FEATURE_USER_CREATE);

	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("User spec must be specified")));
	}

	/*Verify that we have not yet hit the limit of users allowed */
	const char *cmdStr = FormatSqlQuery(
		"SELECT COUNT(*) FROM pg_roles parent JOIN pg_auth_members am ON parent.oid = am.roleid JOIN pg_roles child " \
		"ON am.member = child.oid WHERE child.rolcanlogin = true AND parent.rolname IN ('%s', '%s') " \
		"AND child.rolname NOT IN ('%s', '%s');", ApiAdminRoleV2, ApiReadOnlyRole,
		ApiAdminRoleV2, ApiReadOnlyRole);

	bool readOnly = true;
	bool isNull = false;
	Datum userCountDatum = ExtensionExecuteQueryViaSPI(cmdStr, readOnly,
													   SPI_OK_SELECT, &isNull);
	int userCount = 0;

	if (!isNull)
	{
		userCount = DatumGetInt32(userCountDatum);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Failed to get current user count.")));
	}

	if (userCount >= MaxUserLimit)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_USERCOUNTLIMITEXCEEDED),
						errmsg(
							"Exceeded the limit of %d secondary users. " \
							"For more options, visit https://aka.ms/mongodbvcore-rbac",
							MaxUserLimit)));
	}

	pgbson *createUserSpec = PG_GETARG_PGBSON(0);
	CreateUserSpec *spec = ParseCreateUserSpec(createUserSpec);
	StringInfo createUserInfo = makeStringInfo();
	appendStringInfo(createUserInfo,
					 "CREATE ROLE %s WITH LOGIN PASSWORD '%s' INHERIT IN ROLE %s;",
					 quote_identifier(spec->createUser),
					 PrehashPassword(spec->pwd),
					 quote_identifier(spec->pgRole));

	readOnly = false;
	isNull = false;
	ExtensionExecuteQueryViaSPI(createUserInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	if (strcmp(spec->pgRole, ApiReadOnlyRole) == 0)
	{
		/* This is needed to grant ApiReadOnlyRole */
		/* read access to all new and existing collections */
		resetStringInfo(createUserInfo);
		appendStringInfo(createUserInfo,
						 "GRANT pg_read_all_data TO %s",
						 quote_identifier(spec->createUser));
		ExtensionExecuteQueryViaSPI(createUserInfo->data, readOnly, SPI_OK_UTILITY,
									&isNull);
	}

	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * ParseCreateUserSpec parses the wire
 * protocol message createUser() which creates a mongo user
 */
static CreateUserSpec *
ParseCreateUserSpec(pgbson *createSpec)
{
	bson_iter_t createIter;
	PgbsonInitIterator(createSpec, &createIter);

	CreateUserSpec *spec = palloc0(sizeof(CreateUserSpec));

	bool has_user = false;
	bool has_pwd = false;
	bool has_roles = false;

	while (bson_iter_next(&createIter))
	{
		const char *key = bson_iter_key(&createIter);
		if (strcmp(key, "createUser") == 0)
		{
			EnsureTopLevelFieldType("createUser", &createIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			spec->createUser = bson_iter_utf8(&createIter, &strLength);
			if (strLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"createUser cannot be empty")));
			}

			if (IsUserNameInvalid(spec->createUser))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Invalid user name")));
			}

			has_user = true;
		}
		else if (strcmp(key, "pwd") == 0)
		{
			EnsureTopLevelFieldType("pwd", &createIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			spec->pwd = bson_iter_utf8(&createIter, &strLength);
			if (strLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"pwd cannot be empty")));
			}

			has_pwd = true;
		}
		else if (strcmp(key, "roles") == 0)
		{
			spec->roles = *bson_iter_value(&createIter);

			if (IsBsonValueEmptyDocument(&spec->roles))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"Field roles cannot be an empty document")));
			}

			/* Validate that it is of the right format */
			spec->pgRole = ValidateAndObtainUserRole(&spec->roles);
			has_roles = true;
		}
		else if (!IsCommonSpecIgnoredField(key))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unsupported field specified : %s", key)));
		}
	}

	if (has_user && has_pwd && has_roles)
	{
		return spec;
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg("createUser, pwd and roles are required fields")));
}


/*
 *  At the moment we only allow ApiAdminRole and ApiReadOnlyRole
 *  1. ApiAdminRole corresponds to
 *      roles: [
 *          { role: "clusterAdmin", db: "admin" },
 *          { role: "readWriteAnyDatabase", db: "admin" }
 *      ]
 *
 *  2. ApiReadOnlyRole corresponds to
 *      roles: [
 *          { role: "readAnyDatabase", db: "admin" }
 *      ]
 *
 *  Reject all other combinations.
 */
static char *
ValidateAndObtainUserRole(const bson_value_t *rolesDocument)
{
	bson_iter_t rolesIterator;
	BsonValueInitIterator(rolesDocument, &rolesIterator);
	int userRoles = 0;

	while (bson_iter_next(&rolesIterator))
	{
		bson_iter_t roleIterator;

		BsonValueInitIterator(bson_iter_value(&rolesIterator), &roleIterator);
		while (bson_iter_next(&roleIterator))
		{
			const char *key = bson_iter_key(&roleIterator);

			if (strcmp(key, "role") == 0)
			{
				uint32_t strLength = 0;
				const char *role = bson_iter_utf8(&roleIterator, &strLength);
				if (strcmp(role, "readAnyDatabase") == 0)
				{
					/*This would indicate the ApiReadOnlyRole provided the db is "admin" */
					userRoles |= DocumentDB_Role_Read_AnyDatabase;
				}
				else if (strcmp(role, "readWriteAnyDatabase") == 0)
				{
					/*This would indicate the ApiAdminRole provided the db is "admin" and there is another role "clusterAdmin" */
					userRoles |= DocumentDB_Role_ReadWrite_AnyDatabase;
				}
				else if (strcmp(role, "clusterAdmin") == 0)
				{
					/*This would indicate the ApiAdminRole provided the db is "admin" and there is another role "readWriteAnyDatabase" */
					userRoles |= DocumentDB_Role_Cluster_Admin;
				}
				else
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_ROLENOTFOUND),
									errmsg("Invalid value specified for role: %s", role),
									errdetail_log("Invalid value specified for role: %s",
												  role)));
				}
			}
			else if (strcmp(key, "db") == 0 || strcmp(key, "$db") == 0)
			{
				uint32_t strLength = 0;
				const char *db = bson_iter_utf8(&roleIterator, &strLength);
				if (strcmp(db, "admin") != 0)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
										"Unsupported value specified for db ")));
				}
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Unexpected parameter specified in roles : %s",
									   key),
								errdetail_log(
									"Unexpected parameter specified in roles : %s",
									key)));
			}
		}
	}

	if ((userRoles & DocumentDB_Role_ReadWrite_AnyDatabase) != 0 &&
		(userRoles & DocumentDB_Role_Cluster_Admin) != 0)
	{
		return ApiAdminRoleV2;
	}

	if ((userRoles & DocumentDB_Role_Read_AnyDatabase) != 0)
	{
		return ApiReadOnlyRole;
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_ROLENOTFOUND),
					errmsg(
						"Roles specified are invalid. Only [{role: \"readAnyDatabase\", db: \"admin\"}] or [{role: \"clusterAdmin\", db: \"admin\"}, {role: \"readWriteAnyDatabase\", db: \"admin\"}] are allowed"),
					errdetail_log(
						"Roles specified are invalid. Only [{role: \"readAnyDatabase\", db: \"admin\"}] or [{role: \"clusterAdmin\", db: \"admin\"}, {role: \"readWriteAnyDatabase\", db: \"admin\"}] are allowed")));
}


/*
 * documentdb_extension_drop_user implements the
 * core logic to drop a user
 */
Datum
documentdb_extension_drop_user(PG_FUNCTION_ARGS)
{
	if (!EnableUserCrud)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("DropUser command is not supported"),
						errdetail_log("DropUser command is not supported")));
	}

	ReportFeatureUsage(FEATURE_USER_DROP);

	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("User spec must be specified")));
	}

	pgbson *dropUserSpec = PG_GETARG_PGBSON(0);
	char *dropUser = ParseDropUserSpec(dropUserSpec);
	StringInfo dropUserInfo = makeStringInfo();
	appendStringInfo(dropUserInfo, "DROP ROLE %s;", quote_identifier(dropUser));

	bool readOnly = false;
	bool isNull = false;
	ExtensionExecuteQueryViaSPI(dropUserInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * ParseDropUserSpec parses the wire
 * protocol message dropUser() which drops a mongo user
 */
static char *
ParseDropUserSpec(pgbson *dropSpec)
{
	bson_iter_t dropIter;
	PgbsonInitIterator(dropSpec, &dropIter);

	char *dropUser = NULL;
	while (bson_iter_next(&dropIter))
	{
		const char *key = bson_iter_key(&dropIter);
		if (strcmp(key, "dropUser") == 0)
		{
			EnsureTopLevelFieldType("dropUser", &dropIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			dropUser = (char *) bson_iter_utf8(&dropIter, &strLength);
			if (strLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"dropUser cannot be empty")));
			}

			if (IsUserNameInvalid(dropUser))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Invalid user name")));
			}
		}
		else if (strcmp(key, "lsid") == 0 || strcmp(key, "$db") == 0)
		{
			continue;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unsupported field specified : %s", key)));
		}
	}

	if (dropUser == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("dropUser is a required field")));
	}

	return dropUser;
}


/*
 * documentdb_extension_update_user implements the core logic to update a user.
 * In MongoDB a user with userAdmin privileges or root privileges can change
 * other users passwords. In postgres a superuser can change any users password.
 * A user with CreateRole privileges can change pwds of roles they created. Given
 * that ApiAdminRole has neither create role nor superuser privileges in our case
 * a user can only change their own pwd and no one elses.
 */
Datum
documentdb_extension_update_user(PG_FUNCTION_ARGS)
{
	if (!EnableUserCrud)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("UpdateUser command is not supported"),
						errdetail_log("UpdateUser command is not supported")));
	}

	ReportFeatureUsage(FEATURE_USER_UPDATE);
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("User spec must be specified")));
	}

	pgbson *updateUserSpec = PG_GETARG_PGBSON(0);
	UpdateUserSpec *spec = ParseUpdateUserSpec(updateUserSpec);
	StringInfo updateUserInfo = makeStringInfo();
	appendStringInfo(updateUserInfo, "ALTER USER %s WITH PASSWORD %s;", quote_identifier(
						 spec->updateUser), quote_literal_cstr(PrehashPassword(
																   spec->pwd)));

	bool readOnly = false;
	bool isNull = false;
	ExtensionExecuteQueryViaSPI(updateUserInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);
	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&finalWriter));
}


/*
 * ParseUpdateUserSpec parses the wire
 * protocol message updateUser() which drops a mongo user
 */
static UpdateUserSpec *
ParseUpdateUserSpec(pgbson *updateSpec)
{
	bson_iter_t updateIter;
	PgbsonInitIterator(updateSpec, &updateIter);

	UpdateUserSpec *spec = palloc0(sizeof(UpdateUserSpec));

	bool has_user = false;
	bool has_pwd = false;

	while (bson_iter_next(&updateIter))
	{
		const char *key = bson_iter_key(&updateIter);
		if (strcmp(key, "updateUser") == 0)
		{
			EnsureTopLevelFieldType("updateUser", &updateIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			spec->updateUser = bson_iter_utf8(&updateIter, &strLength);
			if (strLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"updateUser cannot be empty")));
			}

			has_user = true;
		}
		else if (strcmp(key, "pwd") == 0)
		{
			EnsureTopLevelFieldType("pwd", &updateIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			spec->pwd = bson_iter_utf8(&updateIter, &strLength);
			if (strLength == 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg(
									"pwd cannot be empty")));
			}

			has_pwd = true;
		}
		else if (strcmp(key, "lsid") == 0 || strcmp(key, "$db") == 0)
		{
			continue;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unsupported field specified : %s", key)));
		}
	}

	if (has_user && has_pwd)
	{
		return spec;
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
					errmsg("updateUser and pwd are required fields")));
}


/*
 * documentdb_extension_get_users implements the
 * core logic to get user info
 */
Datum
documentdb_extension_get_users(PG_FUNCTION_ARGS)
{
	if (!EnableUserCrud)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("UsersInfo command is not supported"),
						errdetail_log("UsersInfo command is not supported")));
	}

	ReportFeatureUsage(FEATURE_USER_GET);
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("User spec must be specified")));
	}

	char *userName = ParseGetUserSpec(PG_GETARG_PGBSON(0));
	const char *cmdStr = NULL;
	Datum userInfoDatum;

	if (userName == NULL)
	{
		cmdStr = FormatSqlQuery(
			"WITH r AS (SELECT child.rolname::text AS child_role, parent.rolname::text AS parent_role FROM pg_roles parent JOIN pg_auth_members am ON parent.oid = am.roleid JOIN " \
			"pg_roles child ON am.member = child.oid WHERE child.rolcanlogin = true AND parent.rolname IN ('%s', '%s') AND child.rolname NOT IN " \
			"('%s', '%s')) SELECT ARRAY_AGG(%s.row_get_bson(r)) FROM r;",
			ApiAdminRoleV2, ApiReadOnlyRole, ApiAdminRoleV2, ApiReadOnlyRole,
			CoreSchemaName);
		bool readOnly = true;
		bool isNull = false;
		userInfoDatum = ExtensionExecuteQueryViaSPI(cmdStr, readOnly,
													SPI_OK_SELECT, &isNull);
	}
	else
	{
		cmdStr = FormatSqlQuery(
			"WITH r AS (SELECT child.rolname::text AS child_role, parent.rolname::text AS parent_role FROM pg_roles parent JOIN pg_auth_members am ON parent.oid = am.roleid JOIN " \
			"pg_roles child ON am.member = child.oid WHERE child.rolcanlogin = true AND parent.rolname IN ('%s', '%s') AND child.rolname = $1) SELECT " \
			"ARRAY_AGG(%s.row_get_bson(r)) FROM r;", ApiAdminRoleV2, ApiReadOnlyRole,
			CoreSchemaName);
		int argCount = 1;
		Oid argTypes[1];
		Datum argValues[1];

		argTypes[0] = TEXTOID;
		argValues[0] = CStringGetTextDatum(userName);

		bool readOnly = true;
		bool isNull = false;
		userInfoDatum = ExtensionExecuteQueryWithArgsViaSPI(cmdStr, argCount,
															argTypes, argValues, NULL,
															readOnly, SPI_OK_SELECT,
															&isNull);
	}

	pgbson_writer finalWriter;
	PgbsonWriterInit(&finalWriter);

	if (userInfoDatum == (Datum) 0)
	{
		PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
		pgbson *result = PgbsonWriterGetPgbson(&finalWriter);
		PG_RETURN_POINTER(result);
	}

	ArrayType *val_array = DatumGetArrayTypeP(userInfoDatum);
	Datum *val_datums;
	bool *val_is_null_marker;
	int val_count;

	bool arrayByVal = false;
	int elementLength = -1;
	Oid arrayElementType = ARR_ELEMTYPE(val_array);
	deconstruct_array(val_array,
					  arrayElementType, elementLength, arrayByVal,
					  TYPALIGN_INT, &val_datums, &val_is_null_marker,
					  &val_count);

	if (val_count > 0)
	{
		pgbson_array_writer userArrayWriter;
		PgbsonWriterStartArray(&finalWriter, "users", strlen("users"), &userArrayWriter);
		for (int i = 0; i < val_count; i++)
		{
			pgbson_writer userWriter;
			PgbsonWriterInit(&userWriter);

			/* Convert Datum to a bson_t object */
			pgbson *bson_doc = (pgbson *) DatumGetPointer(val_datums[i]);
			bson_iter_t getIter;
			PgbsonInitIterator(bson_doc, &getIter);

			/* Initialize iterator */
			if (bson_iter_find(&getIter, "child_role"))
			{
				if (BSON_ITER_HOLDS_UTF8(&getIter))
				{
					const char *childRole = bson_iter_utf8(&getIter, NULL);
					PgbsonWriterAppendUtf8(&userWriter, "_id", strlen("_id"), psprintf(
											   "admin.%s",
											   childRole));
					PgbsonWriterAppendUtf8(&userWriter, "userId", strlen("userId"),
										   psprintf(
											   "admin.%s", childRole));
					PgbsonWriterAppendUtf8(&userWriter, "user", strlen("user"),
										   childRole);
					PgbsonWriterAppendUtf8(&userWriter, "db", strlen("db"), "admin");
				}
			}
			if (bson_iter_find(&getIter, "parent_role"))
			{
				if (BSON_ITER_HOLDS_UTF8(&getIter))
				{
					const char *parentRole = bson_iter_utf8(&getIter, NULL);
					if (strcmp(parentRole, ApiReadOnlyRole) == 0)
					{
						pgbson_array_writer roleArrayWriter;
						PgbsonWriterStartArray(&userWriter, "roles", strlen("roles"),
											   &roleArrayWriter);
						pgbson_writer roleWriter;
						PgbsonWriterInit(&roleWriter);
						PgbsonWriterAppendUtf8(&roleWriter, "role", strlen("role"),
											   "readAnyDatabase");
						PgbsonWriterAppendUtf8(&roleWriter, "db", strlen("db"), "admin");
						PgbsonArrayWriterWriteDocument(&roleArrayWriter,
													   PgbsonWriterGetPgbson(
														   &roleWriter));
						PgbsonWriterEndArray(&userWriter, &roleArrayWriter);
					}
					else
					{
						pgbson_array_writer roleArrayWriter;
						PgbsonWriterStartArray(&userWriter, "roles", strlen("roles"),
											   &roleArrayWriter);
						pgbson_writer roleWriter;
						PgbsonWriterInit(&roleWriter);
						PgbsonWriterAppendUtf8(&roleWriter, "role", strlen("role"),
											   "readWriteAnyDatabase");
						PgbsonWriterAppendUtf8(&roleWriter, "db", strlen("db"), "admin");
						PgbsonArrayWriterWriteDocument(&roleArrayWriter,
													   PgbsonWriterGetPgbson(
														   &roleWriter));
						PgbsonWriterInit(&roleWriter);
						PgbsonWriterAppendUtf8(&roleWriter, "role", strlen("role"),
											   "clusterAdmin");
						PgbsonWriterAppendUtf8(&roleWriter, "db", strlen("db"), "admin");
						PgbsonArrayWriterWriteDocument(&roleArrayWriter,
													   PgbsonWriterGetPgbson(
														   &roleWriter));
						PgbsonWriterEndArray(&userWriter, &roleArrayWriter);
					}
				}
			}

			PgbsonArrayWriterWriteDocument(&userArrayWriter, PgbsonWriterGetPgbson(
											   &userWriter));
		}

		PgbsonWriterEndArray(&finalWriter, &userArrayWriter);
	}

	PgbsonWriterAppendInt32(&finalWriter, "ok", 2, 1);
	pgbson *result = PgbsonWriterGetPgbson(&finalWriter);
	PG_RETURN_POINTER(result);
}


static char *
ParseGetUserSpec(pgbson *getSpec)
{
	bson_iter_t getIter;
	PgbsonInitIterator(getSpec, &getIter);

	while (bson_iter_next(&getIter))
	{
		const char *key = bson_iter_key(&getIter);
		if (strcmp(key, "usersInfo") == 0)
		{
			if (bson_iter_type(&getIter) == BSON_TYPE_INT32)
			{
				if (bson_iter_as_int64(&getIter) == 1)
				{
					return NULL;
				}
				else
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
									errmsg("Unsupported value for usersInfo")));
				}
			}
			else if (bson_iter_type(&getIter) == BSON_TYPE_UTF8)
			{
				uint32_t strLength = 0;
				return (char *) bson_iter_utf8(&getIter, &strLength);
			}
			else if (bson_iter_type(&getIter) == BSON_TYPE_DOCUMENT)
			{
				const bson_value_t usersInfoBson = *bson_iter_value(&getIter);
				bson_iter_t iter;
				BsonValueInitIterator(&usersInfoBson, &iter);

				while (bson_iter_next(&iter))
				{
					const char *bsonDocKey = bson_iter_key(&iter);
					if (strcmp(bsonDocKey, "db") == 0 && BSON_ITER_HOLDS_UTF8(&iter))
					{
						uint32_t strLength;
						const char *db = bson_iter_utf8(&iter, &strLength);
						if (strcmp(db, "admin") != 0)
						{
							ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
											errmsg(
												"Unsupported value specified for db : %s",
												db),
											errdetail_log(
												"Unsupported value specified for db : %s",
												db)));
						}
					}
					else if (strcmp(bsonDocKey, "user") == 0 && BSON_ITER_HOLDS_UTF8(
								 &iter))
					{
						uint32_t strLength;
						return (char *) bson_iter_utf8(&getIter, &strLength);
					}
				}
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Unusupported value for usersInfo")));
			}
		}
		else if (strcmp(key, "forAllDBs") == 0)
		{
			if (bson_iter_type(&getIter) == BSON_TYPE_BOOL)
			{
				if (bson_iter_as_bool(&getIter) != true)
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
									errmsg("Unusupported value for forAllDBs")));
				}
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Unusupported value for forAllDBs")));
			}

			return NULL;
		}
		else if (strcmp(key, "getUser") == 0)
		{
			EnsureTopLevelFieldType("getUser", &getIter, BSON_TYPE_UTF8);
			uint32_t strLength = 0;
			return (char *) bson_iter_utf8(&getIter, &strLength);
		}
		else if (strcmp(key, "lsid") == 0 || strcmp(key, "$db") == 0)
		{
			continue;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
							errmsg("Unusupported field")));
		}
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
						"Please provide usersInfo or forAllDBs")));
}


/*
 * This method is mostly copied from pg_be_scram_build_secret in PG. The only substantial change
 * is that we use a default salt length of 28 as opposed to 16 used by PG. This is to ensure
 * compatiblity with compass, legacy mongo shell, c drivers, php drivers.
 */
static char *
PrehashPassword(const char *password)
{
	char *prep_password;
	pg_saslprep_rc rc;
	char saltbuf[SCRAM_MAX_SALT_LEN];
	char *result;
	const char *errstr = NULL;

	/*
	 * Validate that the default salt length is not greater than the max salt length allowed
	 */
	if (ScramDefaultSaltLen > SCRAM_MAX_SALT_LEN)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("Invalid value for salt length")));
	}

	/*
	 * Normalize the password with SASLprep.  If that doesn't work, because
	 * the password isn't valid UTF-8 or contains prohibited characters, just
	 * proceed with the original password.  (See comments at top of file.)
	 */
	rc = pg_saslprep(password, &prep_password);
	if (rc == SASLPREP_SUCCESS)
	{
		password = (const char *) prep_password;
	}

	/* Generate random salt */
	if (!pg_strong_random(saltbuf, ScramDefaultSaltLen))
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("could not generate random salt")));
	}

#if PG_VERSION_NUM >= 160000  /* PostgreSQL 16.0 or higher */
	result = scram_build_secret(PG_SHA256, SCRAM_SHA_256_KEY_LEN,
								saltbuf, ScramDefaultSaltLen,
								scram_sha_256_iterations, password,
								&errstr);
#else
	result = scram_build_secret(saltbuf, ScramDefaultSaltLen,
								SCRAM_DEFAULT_ITERATIONS, password,
								&errstr);
#endif

	if (prep_password)
	{
		pfree(prep_password);
	}

	return result;
}


static bool
IsUserNameInvalid(const char *userName)
{
	/* Split the blocked role prefix list */
	char *copyBlockList = pstrdup(BlockedRolePrefixList);
	char *token = strtok(copyBlockList, ",");
	while (token != NULL)
	{
		if (strncmp(userName, token, strlen(token)) == 0)
		{
			pfree(copyBlockList);
			return true;
		}
		token = strtok(NULL, ",");
	}
	pfree(copyBlockList);
	return false;
}
