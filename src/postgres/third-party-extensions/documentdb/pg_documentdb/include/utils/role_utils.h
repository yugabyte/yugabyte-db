#ifndef ROLE_UTILS_H
#define ROLE_UTILS_H

#include "postgres.h"
#include "fmgr.h"

/* Macro to check if a role is a system role */
#define IS_SYSTEM_LOGIN_ROLE(roleName) \
	(strcmp((roleName), ApiBgWorkerRole) == 0 || \
	 strcmp((roleName), ApiReplicationRole) == 0)

/* Macro to check if a role is a customer facing built-in role */
#define IS_BUILTIN_ROLE(roleName) \
	(strcmp((roleName), ApiAdminRoleV2) == 0 || \
	 strcmp((roleName), ApiReadOnlyRole) == 0 || \
	 strcmp((roleName), ApiReadWriteRole) == 0 || \
	 strcmp((roleName), ApiRootRole) == 0 || \
	 strcmp((roleName), ApiUserAdminRole) == 0)

/*
 * Privilege stores a privilege and its actions.
 */
typedef struct
{
	const char *db;
	const char *collection;
	bool isCluster;
	size_t numActions;
	const StringView *actions;
} Privilege;

/*
 * ConsolidatedPrivilege contains the db, collection, isCluster, and actions of a privilege.
 */
typedef struct
{
	const char *db;
	const char *collection;
	bool isCluster;
	HTAB *actions;
} ConsolidatedPrivilege;

/* Function to write a single role's privileges to a BSON array writer */
void WriteSingleRolePrivileges(const char *roleName,
							   pgbson_array_writer *privilegesArrayWriter);

/* Function to write multiple roles' privileges from an HTAB to a BSON array writer*/
void WriteMultipleRolePrivileges(HTAB *rolesTable,
								 pgbson_array_writer *privilegesArrayWriter);

/* Function to check if a given role name contains any reserved pg role name prefixes. */
bool ContainsReservedPgRoleNamePrefix(const char *name);

/* Function to build a List of parent role names from an array of Datums */
List * ConvertUserOrRoleNamesDatumToList(Datum *parentRolesDatums, int parentRolesCount);

#endif
