/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/roles.h
 *
 * Role CRUD functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_ROLES_H
#define EXTENSION_ROLES_H

#include "postgres.h"
#include "utils/string_view.h"

typedef struct
{
	const char *roleName;
	List *parentRoles;
} CreateRoleSpec;

typedef struct
{
	List *roleNames;
	bool showAllRoles;
	bool showBuiltInRoles;
	bool showPrivileges;
} RolesInfoSpec;

typedef struct
{
	const char *roleName;
} DropRoleSpec;

/* Method to create a role */
Datum create_role(pgbson *createRoleBson);

/* Method to drop a role */
Datum drop_role(pgbson *dropRoleBson);

/* Method to get roles information */
Datum roles_info(pgbson *rolesInfoBson);

/* Method to update a role */
Datum update_role(pgbson *updateRoleBson);

#endif
