/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/users.h
 *
 * User CRUD functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_USERS_H
#define EXTENSION_USERS_H

#include "postgres.h"
#include "utils/string_view.h"

enum DocumentDB_BuiltInRoles
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

	/* "identityProvider" field*/
	bson_value_t identityProviderData;

	/* pgRole the passed in role maps to */
	char *pgRole;

	/* principalType */
	char *principalType;

	/* has_identity_provider */
	bool has_identity_provider;
} CreateUserSpec;

typedef struct
{
	/* "updateUser" field */
	const char *updateUser;

	/* "pwd" field */
	const char *pwd;
} UpdateUserSpec;

typedef struct
{
	StringView user;
	bool showAllUsers;
	bool showPrivileges;
} GetUserSpec;

/*
 * Hash entry structure for user roles.
 */
typedef struct UserRoleHashEntry
{
	char *user;
	HTAB *roles;
	bool isExternal;
} UserRoleHashEntry;

/* Method to call Connection Status command */
Datum connection_status(pgbson *showPrivilegesSpec);

#endif
