/*-------------------------------------------------------------------------
 *
 * user.c
 *	  Commands for manipulating roles (formerly called users).
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/backend/commands/user.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <assert.h>

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/binary_upgrade.h"
#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting.h"
#include "commands/comment.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/seclabel.h"
#include "commands/user.h"
#include "libpq/crypt.h"
#include "miscadmin.h"
#include "storage/lmgr.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"

#include "catalog/pg_yb_role_profile.h"
#include "commands/yb_profile.h"
#include "pg_yb_utils.h"

/* Potentially set by pg_upgrade_support functions */
Oid			binary_upgrade_next_pg_authid_oid = InvalidOid;


/* GUC parameter */
int			Password_encryption = PASSWORD_TYPE_SCRAM_SHA_256;

/* Hook to check passwords in CreateRole() and AlterRole() */
check_password_hook_type check_password_hook = NULL;

static void AddRoleMems(const char *rolename, Oid roleid,
						List *memberSpecs, List *memberIds,
						Oid grantorId, bool admin_opt);
static void DelRoleMems(const char *rolename, Oid roleid,
						List *memberSpecs, List *memberIds,
						bool admin_opt);


/* Check if current user has createrole privileges */
static bool
have_createrole_privilege(void)
{
	return has_createrole_privilege(GetUserId());
}


/*
 * CREATE ROLE
 */
Oid
CreateRole(ParseState *pstate, CreateRoleStmt *stmt)
{
	Relation	pg_authid_rel;
	TupleDesc	pg_authid_dsc;
	HeapTuple	tuple;
	Datum		new_record[Natts_pg_authid];
	bool		new_record_nulls[Natts_pg_authid];
	Oid			roleid;
	ListCell   *item;
	ListCell   *option;
	char	   *password = NULL;	/* user password */
	bool		issuper = false;	/* Make the user a superuser? */
	bool		inherit = true; /* Auto inherit privileges? */
	bool		createrole = false; /* Can this user create roles? */
	bool		createdb = false;	/* Can the user create databases? */
	bool		canlogin = false;	/* Can this user login? */
	bool		isreplication = false;	/* Is this a replication role? */
	bool		bypassrls = false;	/* Is this a row security enabled role? */
	int			connlimit = -1; /* maximum connections allowed */
	List	   *addroleto = NIL;	/* roles to make this a member of */
	List	   *rolemembers = NIL;	/* roles to be members of this role */
	List	   *adminmembers = NIL; /* roles to be admins of this role */
	char	   *validUntil = NULL;	/* time the login is valid until */
	Datum		validUntil_datum;	/* same, as timestamptz Datum */
	bool		validUntil_null;
	DefElem    *dpassword = NULL;
	DefElem    *dissuper = NULL;
	DefElem    *dinherit = NULL;
	DefElem    *dcreaterole = NULL;
	DefElem    *dcreatedb = NULL;
	DefElem    *dcanlogin = NULL;
	DefElem    *disreplication = NULL;
	DefElem    *dconnlimit = NULL;
	DefElem    *daddroleto = NULL;
	DefElem    *drolemembers = NULL;
	DefElem    *dadminmembers = NULL;
	DefElem    *dvalidUntil = NULL;
	DefElem    *dbypassRLS = NULL;

	/* The defaults can vary depending on the original statement type */
	switch (stmt->stmt_type)
	{
		case ROLESTMT_ROLE:
			break;
		case ROLESTMT_USER:
			canlogin = true;
			/* may eventually want inherit to default to false here */
			break;
		case ROLESTMT_GROUP:
			break;
	}

	/* Extract options from the statement node tree */
	foreach(option, stmt->options)
	{
		DefElem    *defel = (DefElem *) lfirst(option);

		if (strcmp(defel->defname, "password") == 0)
		{
			if (dpassword)
				errorConflictingDefElem(defel, pstate);
			dpassword = defel;
		}
		else if (strcmp(defel->defname, "sysid") == 0)
		{
			ereport(NOTICE,
					(errmsg("SYSID can no longer be specified")));
		}
		else if (strcmp(defel->defname, "superuser") == 0)
		{
			if (dissuper)
				errorConflictingDefElem(defel, pstate);
			dissuper = defel;
		}
		else if (strcmp(defel->defname, "inherit") == 0)
		{
			if (dinherit)
				errorConflictingDefElem(defel, pstate);
			dinherit = defel;
		}
		else if (strcmp(defel->defname, "createrole") == 0)
		{
			if (dcreaterole)
				errorConflictingDefElem(defel, pstate);
			dcreaterole = defel;
		}
		else if (strcmp(defel->defname, "createdb") == 0)
		{
			if (dcreatedb)
				errorConflictingDefElem(defel, pstate);
			dcreatedb = defel;
		}
		else if (strcmp(defel->defname, "canlogin") == 0)
		{
			if (dcanlogin)
				errorConflictingDefElem(defel, pstate);
			dcanlogin = defel;
		}
		else if (strcmp(defel->defname, "isreplication") == 0)
		{
			if (disreplication)
				errorConflictingDefElem(defel, pstate);
			disreplication = defel;
		}
		else if (strcmp(defel->defname, "connectionlimit") == 0)
		{
			if (dconnlimit)
				errorConflictingDefElem(defel, pstate);
			dconnlimit = defel;
		}
		else if (strcmp(defel->defname, "addroleto") == 0)
		{
			if (daddroleto)
				errorConflictingDefElem(defel, pstate);
			daddroleto = defel;
		}
		else if (strcmp(defel->defname, "rolemembers") == 0)
		{
			if (drolemembers)
				errorConflictingDefElem(defel, pstate);
			drolemembers = defel;
		}
		else if (strcmp(defel->defname, "adminmembers") == 0)
		{
			if (dadminmembers)
				errorConflictingDefElem(defel, pstate);
			dadminmembers = defel;
		}
		else if (strcmp(defel->defname, "validUntil") == 0)
		{
			if (dvalidUntil)
				errorConflictingDefElem(defel, pstate);
			dvalidUntil = defel;
		}
		else if (strcmp(defel->defname, "bypassrls") == 0)
		{
			if (dbypassRLS)
				errorConflictingDefElem(defel, pstate);
			dbypassRLS = defel;
		}
		else
			elog(ERROR, "option \"%s\" not recognized",
				 defel->defname);
	}

	if (dpassword && dpassword->arg)
		password = strVal(dpassword->arg);
	if (dissuper)
		issuper = boolVal(dissuper->arg);
	if (dinherit)
		inherit = boolVal(dinherit->arg);
	if (dcreaterole)
		createrole = boolVal(dcreaterole->arg);
	if (dcreatedb)
		createdb = boolVal(dcreatedb->arg);
	if (dcanlogin)
		canlogin = boolVal(dcanlogin->arg);
	if (disreplication)
		isreplication = boolVal(disreplication->arg);
	if (dconnlimit)
	{
		connlimit = intVal(dconnlimit->arg);
		if (connlimit < -1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid connection limit: %d", connlimit)));
	}
	if (daddroleto)
		addroleto = (List *) daddroleto->arg;
	if (drolemembers)
		rolemembers = (List *) drolemembers->arg;
	if (dadminmembers)
		adminmembers = (List *) dadminmembers->arg;
	if (dvalidUntil)
		validUntil = strVal(dvalidUntil->arg);
	if (dbypassRLS)
		bypassrls = boolVal(dbypassRLS->arg);

	/* Check some permissions first */
	if (issuper)
	{
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to create superusers")));
	}
	else if (isreplication)
	{
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to create replication users")));
	}
	else if (bypassrls)
	{
		if (!superuser() && !IsYbDbAdminUser(GetUserId()))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser or a member of the yb_db_admin "
							"role to create bypassrls users")));
	}
	else
	{
		if (!have_createrole_privilege())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("permission denied to create role")));
	}

	/*
	 * Check that the user is not trying to create a role in the reserved
	 * "pg_" namespace.
	 */
	if (IsReservedName(stmt->role))
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("role name \"%s\" is reserved",
						stmt->role),
				 errdetail("Role names starting with \"pg_\" are reserved.")));

	/*
	 * If built with appropriate switch, whine when regression-testing
	 * conventions for role names are violated.
	 */
#ifdef ENFORCE_REGRESSION_TEST_NAME_RESTRICTIONS
	if (strncmp(stmt->role, "regress_", 8) != 0)
		elog(WARNING, "roles created by regression test cases should have names starting with \"regress_\"");
#endif

	/*
	 * Check the pg_authid relation to be certain the role doesn't already
	 * exist.
	 */
	pg_authid_rel = table_open(AuthIdRelationId, RowExclusiveLock);
	pg_authid_dsc = RelationGetDescr(pg_authid_rel);

	if (OidIsValid(get_role_oid(stmt->role, true)))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("role \"%s\" already exists",
						stmt->role)));

	/* Convert validuntil to internal form */
	if (validUntil)
	{
		validUntil_datum = DirectFunctionCall3(timestamptz_in,
											   CStringGetDatum(validUntil),
											   ObjectIdGetDatum(InvalidOid),
											   Int32GetDatum(-1));
		validUntil_null = false;
	}
	else
	{
		validUntil_datum = (Datum) 0;
		validUntil_null = true;
	}

	/*
	 * Call the password checking hook if there is one defined
	 */
	if (check_password_hook && password)
		(*check_password_hook) (stmt->role,
								password,
								get_password_type(password),
								validUntil_datum,
								validUntil_null);

	/*
	 * Build a tuple to insert
	 */
	MemSet(new_record, 0, sizeof(new_record));
	MemSet(new_record_nulls, false, sizeof(new_record_nulls));

	new_record[Anum_pg_authid_rolname - 1] =
		DirectFunctionCall1(namein, CStringGetDatum(stmt->role));

	new_record[Anum_pg_authid_rolsuper - 1] = BoolGetDatum(issuper);
	new_record[Anum_pg_authid_rolinherit - 1] = BoolGetDatum(inherit);
	new_record[Anum_pg_authid_rolcreaterole - 1] = BoolGetDatum(createrole);
	new_record[Anum_pg_authid_rolcreatedb - 1] = BoolGetDatum(createdb);
	new_record[Anum_pg_authid_rolcanlogin - 1] = BoolGetDatum(canlogin);
	new_record[Anum_pg_authid_rolreplication - 1] = BoolGetDatum(isreplication);
	new_record[Anum_pg_authid_rolconnlimit - 1] = Int32GetDatum(connlimit);

	if (password)
	{
		char	   *shadow_pass;
		const char *logdetail = NULL;

		/*
		 * Don't allow an empty password. Libpq treats an empty password the
		 * same as no password at all, and won't even try to authenticate. But
		 * other clients might, so allowing it would be confusing. By clearing
		 * the password when an empty string is specified, the account is
		 * consistently locked for all clients.
		 *
		 * Note that this only covers passwords stored in the database itself.
		 * There are also checks in the authentication code, to forbid an
		 * empty password from being used with authentication methods that
		 * fetch the password from an external system, like LDAP or PAM.
		 */
		if (password[0] == '\0' ||
			plain_crypt_verify(stmt->role, password, "", &logdetail) == STATUS_OK)
		{
			ereport(NOTICE,
					(errmsg("empty string is not a valid password, clearing password")));
			new_record_nulls[Anum_pg_authid_rolpassword - 1] = true;
		}
		else
		{
			/* Encrypt the password to the requested format. */
			shadow_pass = encrypt_password(Password_encryption, stmt->role,
										   password);
			new_record[Anum_pg_authid_rolpassword - 1] =
				CStringGetTextDatum(shadow_pass);
		}
	}
	else
		new_record_nulls[Anum_pg_authid_rolpassword - 1] = true;

	new_record[Anum_pg_authid_rolvaliduntil - 1] = validUntil_datum;
	new_record_nulls[Anum_pg_authid_rolvaliduntil - 1] = validUntil_null;

	new_record[Anum_pg_authid_rolbypassrls - 1] = BoolGetDatum(bypassrls);

	/*
	 * pg_largeobject_metadata contains pg_authid.oid's, so we use the
	 * binary-upgrade override.
	 */
	if (IsBinaryUpgrade && !yb_binary_restore)
	{
		if (!OidIsValid(binary_upgrade_next_pg_authid_oid))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("pg_authid OID value not set when in binary upgrade mode")));

		roleid = binary_upgrade_next_pg_authid_oid;
		binary_upgrade_next_pg_authid_oid = InvalidOid;
	}
	else
	{
		roleid = GetNewOidWithIndex(pg_authid_rel, AuthIdOidIndexId,
									Anum_pg_authid_oid);
	}

	new_record[Anum_pg_authid_oid - 1] = ObjectIdGetDatum(roleid);

	tuple = heap_form_tuple(pg_authid_dsc, new_record, new_record_nulls);

	/*
	 * Insert new record in the pg_authid table
	 */
	CatalogTupleInsert(pg_authid_rel, tuple);

	/*
	 * Advance command counter so we can see new record; else tests in
	 * AddRoleMems may fail.
	 */
	if (addroleto || adminmembers || rolemembers)
		CommandCounterIncrement();

	/*
	 * Add the new role to the specified existing roles.
	 */
	if (addroleto)
	{
		RoleSpec   *thisrole = makeNode(RoleSpec);
		List	   *thisrole_list = list_make1(thisrole);
		List	   *thisrole_oidlist = list_make1_oid(roleid);

		thisrole->roletype = ROLESPEC_CSTRING;
		thisrole->rolename = stmt->role;
		thisrole->location = -1;

		foreach(item, addroleto)
		{
			RoleSpec   *oldrole = lfirst(item);
			HeapTuple	oldroletup = get_rolespec_tuple(oldrole);
			Form_pg_authid oldroleform = (Form_pg_authid) GETSTRUCT(oldroletup);
			Oid			oldroleid = oldroleform->oid;
			char	   *oldrolename = NameStr(oldroleform->rolname);

			AddRoleMems(oldrolename, oldroleid,
						thisrole_list,
						thisrole_oidlist,
						GetUserId(), false);

			ReleaseSysCache(oldroletup);
		}
	}

	/*
	 * Add the specified members to this new role. adminmembers get the admin
	 * option, rolemembers don't.
	 */
	AddRoleMems(stmt->role, roleid,
				adminmembers, roleSpecsToIds(adminmembers),
				GetUserId(), true);
	AddRoleMems(stmt->role, roleid,
				rolemembers, roleSpecsToIds(rolemembers),
				GetUserId(), false);

	/* Post creation hook for new role */
	InvokeObjectPostCreateHook(AuthIdRelationId, roleid, 0);

	/*
	 * Close pg_authid, but keep lock till commit.
	 */
	table_close(pg_authid_rel, NoLock);

	return roleid;
}

/*
 * This function must be kept in sync with the struct FormData_pg_authid.
 * C provides no language facilities to do struct equality check so we
 * compare each structure member by member.
 */
static bool
YbIsPgAuthTupleEqual(TupleDesc pg_authid_dsc,
					 HeapTuple auth_tup1,
					 HeapTuple auth_tup2)
{
	static_assert(sizeof(*(Form_pg_authid)0) == 80, "size mismatch");
	Form_pg_authid authform1 = (Form_pg_authid) GETSTRUCT(auth_tup1);
	Form_pg_authid authform2 = (Form_pg_authid) GETSTRUCT(auth_tup2);
	/* All these struct members have a not null constraint. */
	if (authform1->oid != authform2->oid ||
		authform1->rolsuper != authform2->rolsuper ||
		authform1->rolinherit != authform2->rolinherit ||
		authform1->rolcreaterole != authform2->rolcreaterole ||
		authform1->rolcreatedb != authform2->rolcreatedb ||
		authform1->rolcanlogin != authform2->rolcanlogin ||
		authform1->rolreplication != authform2->rolreplication ||
		authform1->rolbypassrls != authform2->rolbypassrls ||
		authform1->rolconnlimit != authform2->rolconnlimit)
		return false;
	if (strcmp(NameStr(authform1->rolname), NameStr(authform2->rolname)))
		return false;
#ifdef CATALOG_VARLEN
#error "need to compare extra members"
#endif
	Datum datum1, datum2;
	bool isnull1, isnull2;

	/* Check rolpassword (SQL type text), can be null. */
	datum1 = heap_getattr(auth_tup1, Anum_pg_authid_rolpassword,
						  pg_authid_dsc, &isnull1);
	datum2 = heap_getattr(auth_tup2, Anum_pg_authid_rolpassword,
						  pg_authid_dsc, &isnull2);
	if (isnull1 != isnull2)
		return false;
	if (!isnull1 &&
		strcmp(TextDatumGetCString(datum1), TextDatumGetCString(datum2)))
		return false;

	/*
	 * Check rolvaliduntil (SQL type timestamp with time zone, can be null.
	 */
	datum1 = heap_getattr(auth_tup1, Anum_pg_authid_rolvaliduntil,
						  pg_authid_dsc, &isnull1);
	datum2 = heap_getattr(auth_tup2, Anum_pg_authid_rolvaliduntil,
						  pg_authid_dsc, &isnull2);
	if (isnull1 != isnull2)
		return false;
	if (!isnull1 && DatumGetTimestampTz(datum1) != DatumGetTimestampTz(datum2))
		return false;
	return true;
}


/*
 * ALTER ROLE
 *
 * Note: the rolemembers option accepted here is intended to support the
 * backwards-compatible ALTER GROUP syntax.  Although it will work to say
 * "ALTER ROLE role ROLE rolenames", we don't document it.
 */
Oid
AlterRole(ParseState *pstate, AlterRoleStmt *stmt)
{
	Datum		new_record[Natts_pg_authid];
	bool		new_record_nulls[Natts_pg_authid];
	bool		new_record_repl[Natts_pg_authid];
	Relation	pg_authid_rel;
	TupleDesc	pg_authid_dsc;
	HeapTuple	tuple,
				new_tuple;
	Form_pg_authid authform;
	ListCell   *option;
	char	   *rolename;
	char	   *password = NULL;	/* user password */
	int			connlimit = -1; /* maximum connections allowed */
	char	   *validUntil = NULL;	/* time the login is valid until */
	Datum		validUntil_datum;	/* same, as timestamptz Datum */
	bool		validUntil_null;
	DefElem    *dpassword = NULL;
	DefElem    *dissuper = NULL;
	DefElem    *dinherit = NULL;
	DefElem    *dcreaterole = NULL;
	DefElem    *dcreatedb = NULL;
	DefElem    *dcanlogin = NULL;
	DefElem    *disreplication = NULL;
	DefElem    *dconnlimit = NULL;
	DefElem    *drolemembers = NULL;
	DefElem    *dvalidUntil = NULL;
	DefElem    *dbypassRLS = NULL;
	Oid			roleid;

	char       *profile = NULL;
	int			unlocked = -1;
	DefElem    *dprofile = NULL;
	DefElem    *dnoprofile = NULL;
	DefElem    *dunlocked = NULL;

	check_rolespec_name(stmt->role,
						_("Cannot alter reserved roles."));

	/* Extract options from the statement node tree */
	foreach(option, stmt->options)
	{
		DefElem    *defel = (DefElem *) lfirst(option);

		if (strcmp(defel->defname, "password") == 0)
		{
			if (dpassword)
				errorConflictingDefElem(defel, pstate);
			dpassword = defel;
		}
		else if (strcmp(defel->defname, "superuser") == 0)
		{
			if (dissuper)
				errorConflictingDefElem(defel, pstate);
			dissuper = defel;
		}
		else if (strcmp(defel->defname, "inherit") == 0)
		{
			if (dinherit)
				errorConflictingDefElem(defel, pstate);
			dinherit = defel;
		}
		else if (strcmp(defel->defname, "createrole") == 0)
		{
			if (dcreaterole)
				errorConflictingDefElem(defel, pstate);
			dcreaterole = defel;
		}
		else if (strcmp(defel->defname, "createdb") == 0)
		{
			if (dcreatedb)
				errorConflictingDefElem(defel, pstate);
			dcreatedb = defel;
		}
		else if (strcmp(defel->defname, "canlogin") == 0)
		{
			if (dcanlogin)
				errorConflictingDefElem(defel, pstate);
			dcanlogin = defel;
		}
		else if (strcmp(defel->defname, "isreplication") == 0)
		{
			if (disreplication)
				errorConflictingDefElem(defel, pstate);
			disreplication = defel;
		}
		else if (strcmp(defel->defname, "connectionlimit") == 0)
		{
			if (dconnlimit)
				errorConflictingDefElem(defel, pstate);
			dconnlimit = defel;
		}
		else if (strcmp(defel->defname, "rolemembers") == 0 &&
				 stmt->action != 0)
		{
			if (drolemembers)
				errorConflictingDefElem(defel, pstate);
			drolemembers = defel;
		}
		else if (strcmp(defel->defname, "validUntil") == 0)
		{
			if (dvalidUntil)
				errorConflictingDefElem(defel, pstate);
			dvalidUntil = defel;
		}
		else if (strcmp(defel->defname, "bypassrls") == 0)
		{
			if (dbypassRLS)
				errorConflictingDefElem(defel, pstate);
			dbypassRLS = defel;
		}
		else if (strcmp(defel->defname, "profile") == 0)
		{
			if (dprofile)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			dprofile = defel;
		}
		else if (strcmp(defel->defname, "noprofile") == 0)
		{
			if (dnoprofile)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			dnoprofile = defel;
		}
		else if (strcmp(defel->defname, "unlocked") == 0)
		{
			if (dunlocked)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			dunlocked = defel;
		}
		else
			elog(ERROR, "option \"%s\" not recognized",
				 defel->defname);
	}

	if (dpassword && dpassword->arg)
		password = strVal(dpassword->arg);
	if (dconnlimit)
	{
		connlimit = intVal(dconnlimit->arg);
		if (connlimit < -1)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("invalid connection limit: %d", connlimit)));
	}
	if (dvalidUntil)
		validUntil = strVal(dvalidUntil->arg);

	if (dprofile && dprofile->arg)
		profile = strVal(dprofile->arg);
	if (dunlocked && dunlocked->arg)
		unlocked = intVal(dunlocked->arg);

	/*
	 * Scan the pg_authid relation to be certain the user exists.
	 */
	pg_authid_rel = table_open(AuthIdRelationId, RowExclusiveLock);
	pg_authid_dsc = RelationGetDescr(pg_authid_rel);

	tuple = get_rolespec_tuple(stmt->role);
	authform = (Form_pg_authid) GETSTRUCT(tuple);
	rolename = pstrdup(NameStr(authform->rolname));
	roleid = authform->oid;

	/*
	 * To mess with a superuser or replication role in any way you gotta be
	 * superuser.  We also insist on superuser to change the BYPASSRLS
	 * property.  Otherwise, if you don't have createrole, you're only allowed
	 * to change your own password.
	 */
	if (authform->rolsuper || dissuper)
	{
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to alter superuser roles or change superuser attribute")));
	}
	else if (authform->rolreplication || disreplication)
	{
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to alter replication roles or change replication attribute")));
	}
	else if (dbypassRLS)
	{
		if (!superuser() && !IsYbDbAdminUser(GetUserId()))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser or a member of the yb_db_admin "
							"role to change bypassrls attribute")));
	}
	else if (profile != NULL || dnoprofile != NULL || dunlocked != NULL)
	{
		if (!superuser() && !IsYbDbAdminUser(GetUserId()))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser or a member of the yb_db_admin "
							"role to change profile configuration")));
	}
	else if (!have_createrole_privilege())
	{
		/* check the rest */
		if (dinherit || dcreaterole || dcreatedb || dcanlogin || dconnlimit ||
			drolemembers || dvalidUntil || !dpassword || roleid != GetUserId())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("permission denied")));
	}

	if (profile != NULL || dnoprofile != NULL || dunlocked != NULL)
	{
		if (profile != NULL)
			YbCreateRoleProfile(roleid, rolename, profile);
		else if (dunlocked != NULL)
			YbSetRoleProfileStatus(roleid, rolename,
								   unlocked == 0 ? YB_ROLPRFSTATUS_LOCKED
												 : YB_ROLPRFSTATUS_OPEN);
		else
		{
			Assert(dnoprofile);
			YbRemoveRoleProfileForRoleIfExists(roleid);
		}

		ReleaseSysCache(tuple);
		table_close(pg_authid_rel, NoLock);
		return roleid;
	}

	/* Convert validuntil to internal form */
	if (dvalidUntil)
	{
		validUntil_datum = DirectFunctionCall3(timestamptz_in,
											   CStringGetDatum(validUntil),
											   ObjectIdGetDatum(InvalidOid),
											   Int32GetDatum(-1));
		validUntil_null = false;
	}
	else
	{
		/* fetch existing setting in case hook needs it */
		validUntil_datum = SysCacheGetAttr(AUTHNAME, tuple,
										   Anum_pg_authid_rolvaliduntil,
										   &validUntil_null);
	}

	/*
	 * Call the password checking hook if there is one defined
	 */
	if (check_password_hook && password)
		(*check_password_hook) (rolename,
								password,
								get_password_type(password),
								validUntil_datum,
								validUntil_null);

	/*
	 * Build an updated tuple, perusing the information just obtained
	 */
	MemSet(new_record, 0, sizeof(new_record));
	MemSet(new_record_nulls, false, sizeof(new_record_nulls));
	MemSet(new_record_repl, false, sizeof(new_record_repl));

	/*
	 * issuper/createrole/etc
	 */
	if (dissuper)
	{
		new_record[Anum_pg_authid_rolsuper - 1] = BoolGetDatum(boolVal(dissuper->arg));
		new_record_repl[Anum_pg_authid_rolsuper - 1] = true;
	}

	if (dinherit)
	{
		new_record[Anum_pg_authid_rolinherit - 1] = BoolGetDatum(boolVal(dinherit->arg));
		new_record_repl[Anum_pg_authid_rolinherit - 1] = true;
	}

	if (dcreaterole)
	{
		new_record[Anum_pg_authid_rolcreaterole - 1] = BoolGetDatum(boolVal(dcreaterole->arg));
		new_record_repl[Anum_pg_authid_rolcreaterole - 1] = true;
	}

	if (dcreatedb)
	{
		new_record[Anum_pg_authid_rolcreatedb - 1] = BoolGetDatum(boolVal(dcreatedb->arg));
		new_record_repl[Anum_pg_authid_rolcreatedb - 1] = true;
	}

	if (dcanlogin)
	{
		new_record[Anum_pg_authid_rolcanlogin - 1] = BoolGetDatum(boolVal(dcanlogin->arg));
		new_record_repl[Anum_pg_authid_rolcanlogin - 1] = true;
	}

	if (disreplication)
	{
		new_record[Anum_pg_authid_rolreplication - 1] = BoolGetDatum(boolVal(disreplication->arg));
		new_record_repl[Anum_pg_authid_rolreplication - 1] = true;
	}

	if (dconnlimit)
	{
		/* Check connection limit for postgres. */
		if (roleid == 10 && connlimit != -1)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("cannot set connection limit for postgres"),
					 errhint("Did you mean ALTER ROLE %s CONNECTION LIMIT -1.", rolename)));
		new_record[Anum_pg_authid_rolconnlimit - 1] = Int32GetDatum(connlimit);
		new_record_repl[Anum_pg_authid_rolconnlimit - 1] = true;
	}

	/* password */
	if (password)
	{
		char	   *shadow_pass;
		const char *logdetail = NULL;

		/* Like in CREATE USER, don't allow an empty password. */
		if (password[0] == '\0' ||
			plain_crypt_verify(rolename, password, "", &logdetail) == STATUS_OK)
		{
			ereport(NOTICE,
					(errmsg("empty string is not a valid password, clearing password")));
			new_record_nulls[Anum_pg_authid_rolpassword - 1] = true;
		}
		else
		{
			/* Encrypt the password to the requested format. */
			shadow_pass = encrypt_password(Password_encryption, rolename,
										   password);
			new_record[Anum_pg_authid_rolpassword - 1] =
				CStringGetTextDatum(shadow_pass);
		}
		new_record_repl[Anum_pg_authid_rolpassword - 1] = true;
	}

	/* unset password */
	if (dpassword && dpassword->arg == NULL)
	{
		new_record_repl[Anum_pg_authid_rolpassword - 1] = true;
		new_record_nulls[Anum_pg_authid_rolpassword - 1] = true;
	}

	/* valid until */
	new_record[Anum_pg_authid_rolvaliduntil - 1] = validUntil_datum;
	new_record_nulls[Anum_pg_authid_rolvaliduntil - 1] = validUntil_null;
	new_record_repl[Anum_pg_authid_rolvaliduntil - 1] = true;

	if (dbypassRLS)
	{
		new_record[Anum_pg_authid_rolbypassrls - 1] = BoolGetDatum(boolVal(dbypassRLS->arg));
		new_record_repl[Anum_pg_authid_rolbypassrls - 1] = true;
	}

	new_tuple = heap_modify_tuple(tuple, pg_authid_dsc, new_record,
								  new_record_nulls, new_record_repl);
	/*
	 * The new_record_repl means which attribute of the tuple this ALTER ROLE
	 * statement has provided a value, new_record_repl[i] is true does not mean
	 * the provided value is different from the current value. Also sometimes
	 * PG just sets new_record_repl[i] to true to indicate that an attribute as
	 * changed even if it no value is provided in the ALTER ROLE statement
	 * (e.g., for Anum_pg_authid_rolvaliduntil). So we do the deep comparison
	 * to check whether there is any real change in new_tuple.
	 */
	if (!IsYugaByteEnabled() ||
		!yb_enable_nop_alter_role_optimization ||
		!YbIsPgAuthTupleEqual(pg_authid_dsc, tuple, new_tuple))
		CatalogTupleUpdate(pg_authid_rel, &tuple->t_self, new_tuple);

	InvokeObjectPostAlterHook(AuthIdRelationId, roleid, 0);

	ReleaseSysCache(tuple);
	heap_freetuple(new_tuple);

	/*
	 * Advance command counter so we can see new record; else tests in
	 * AddRoleMems may fail.
	 */
	if (drolemembers)
	{
		List	   *rolemembers = (List *) drolemembers->arg;

		CommandCounterIncrement();

		if (stmt->action == +1) /* add members to role */
			AddRoleMems(rolename, roleid,
						rolemembers, roleSpecsToIds(rolemembers),
						GetUserId(), false);
		else if (stmt->action == -1)	/* drop members from role */
			DelRoleMems(rolename, roleid,
						rolemembers, roleSpecsToIds(rolemembers),
						false);
	}

	/*
	 * Close pg_authid, but keep lock till commit.
	 */
	table_close(pg_authid_rel, NoLock);

	return roleid;
}


/*
 * ALTER ROLE ... SET
 */
Oid
AlterRoleSet(AlterRoleSetStmt *stmt)
{
	HeapTuple	roletuple;
	Form_pg_authid roleform;
	Oid			databaseid = InvalidOid;
	Oid			roleid = InvalidOid;

	if (stmt->role)
	{
		check_rolespec_name(stmt->role,
							_("Cannot alter reserved roles."));

		roletuple = get_rolespec_tuple(stmt->role);
		roleform = (Form_pg_authid) GETSTRUCT(roletuple);
		roleid = roleform->oid;

		/*
		 * Obtain a lock on the role and make sure it didn't go away in the
		 * meantime.
		 */
		shdepLockAndCheckObject(AuthIdRelationId, roleid);

		/*
		 * To mess with a superuser you gotta be superuser; else you need
		 * createrole, or just want to change your own settings
		 */
		if (roleform->rolsuper)
		{
			if (!superuser())
				ereport(ERROR,
						(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
						 errmsg("must be superuser to alter superusers")));
		}
		else
		{
			if (!have_createrole_privilege() && roleid != GetUserId())
				ereport(ERROR,
						(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
						 errmsg("permission denied")));
		}

		ReleaseSysCache(roletuple);
	}

	/* look up and lock the database, if specified */
	if (stmt->database != NULL)
	{
		databaseid = get_database_oid(stmt->database, false);
		shdepLockAndCheckObject(DatabaseRelationId, databaseid);

		if (!stmt->role)
		{
			/*
			 * If no role is specified, then this is effectively the same as
			 * ALTER DATABASE ... SET, so use the same permission check.
			 */
			if (!pg_database_ownercheck(databaseid, GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_DATABASE,
							   stmt->database);
		}
	}

	if (!stmt->role && !stmt->database)
	{
		/* Must be superuser to alter settings globally. */
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to alter settings globally")));
	}

	AlterSetting(databaseid, roleid, stmt->setstmt);

	return roleid;
}


/*
 * DROP ROLE
 */
void
DropRole(DropRoleStmt *stmt)
{
	Relation	pg_authid_rel,
				pg_auth_members_rel;
	ListCell   *item;

	if (!have_createrole_privilege())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to drop role")));

	/*
	 * Scan the pg_authid relation to find the Oid of the role(s) to be
	 * deleted.
	 */
	pg_authid_rel = table_open(AuthIdRelationId, RowExclusiveLock);
	pg_auth_members_rel = table_open(AuthMemRelationId, RowExclusiveLock);

	foreach(item, stmt->roles)
	{
		RoleSpec   *rolspec = lfirst(item);
		char	   *role;
		HeapTuple	tuple,
					tmp_tuple;
		Form_pg_authid roleform;
		ScanKeyData scankey;
		char	   *detail;
		char	   *detail_log;
		SysScanDesc sscan;
		Oid			roleid;

		if (rolspec->roletype != ROLESPEC_CSTRING)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("cannot use special role specifier in DROP ROLE")));
		role = rolspec->rolename;

		tuple = SearchSysCache1(AUTHNAME, PointerGetDatum(role));
		if (!HeapTupleIsValid(tuple))
		{
			if (!stmt->missing_ok)
			{
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_OBJECT),
						 errmsg("role \"%s\" does not exist", role)));
			}
			else
			{
				ereport(NOTICE,
						(errmsg("role \"%s\" does not exist, skipping",
								role)));
			}

			continue;
		}

		roleform = (Form_pg_authid) GETSTRUCT(tuple);
		roleid = roleform->oid;

		if (roleid == GetUserId())
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_IN_USE),
					 errmsg("current user cannot be dropped")));
		if (roleid == GetOuterUserId())
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_IN_USE),
					 errmsg("current user cannot be dropped")));
		if (roleid == GetSessionUserId())
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_IN_USE),
					 errmsg("session user cannot be dropped")));

		/*
		 * For safety's sake, we allow createrole holders to drop ordinary
		 * roles but not superuser roles.  This is mainly to avoid the
		 * scenario where you accidentally drop the last superuser.
		 */
		if (roleform->rolsuper && !superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to drop superusers")));

		/* DROP hook for the role being removed */
		InvokeObjectDropHook(AuthIdRelationId, roleid, 0);

		/*
		 * Lock the role, so nobody can add dependencies to her while we drop
		 * her.  We keep the lock until the end of transaction.
		 */
		LockSharedObject(AuthIdRelationId, roleid, 0, AccessExclusiveLock);

		/* Check for pg_shdepend entries depending on this role */
		if (checkSharedDependencies(AuthIdRelationId, roleid,
									&detail, &detail_log))
		{
			if (IsYugaByteEnabled() && detail != NULL)
			{
				detail = YBDetailSorted(detail);
			}
			ereport(ERROR,
					(errcode(ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST),
					 errmsg("role \"%s\" cannot be dropped because some objects depend on it",
							role),
					 errdetail_internal("%s", detail),
					 errdetail_log("%s", detail_log)));
		}

		/*
		 * If the role is attached to a profile, auto-remove that association.
		 */
		if (*YBCGetGFlags()->ysql_enable_profile && YbLoginProfileCatalogsExist)
			YbRemoveRoleProfileForRoleIfExists(roleid);

		/*
		 * Remove the role from the pg_authid table
		 */
		CatalogTupleDelete(pg_authid_rel, tuple);

		ReleaseSysCache(tuple);

		/*
		 * Remove role from the pg_auth_members table.  We have to remove all
		 * tuples that show it as either a role or a member.
		 *
		 * XXX what about grantor entries?	Maybe we should do one heap scan.
		 */
		ScanKeyInit(&scankey,
					Anum_pg_auth_members_roleid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(roleid));

		sscan = systable_beginscan(pg_auth_members_rel, AuthMemRoleMemIndexId,
								   true, NULL, 1, &scankey);

		while (HeapTupleIsValid(tmp_tuple = systable_getnext(sscan)))
		{
			CatalogTupleDelete(pg_auth_members_rel, tmp_tuple);
		}

		systable_endscan(sscan);

		ScanKeyInit(&scankey,
					Anum_pg_auth_members_member,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(roleid));

		sscan = systable_beginscan(pg_auth_members_rel, AuthMemMemRoleIndexId,
								   true, NULL, 1, &scankey);

		while (HeapTupleIsValid(tmp_tuple = systable_getnext(sscan)))
		{
			CatalogTupleDelete(pg_auth_members_rel, tmp_tuple);
		}

		systable_endscan(sscan);

		/*
		 * Remove any comments or security labels on this role.
		 */
		DeleteSharedComments(roleid, AuthIdRelationId);
		DeleteSharedSecurityLabel(roleid, AuthIdRelationId);

		/*
		 * Remove settings for this role.
		 */
		DropSetting(InvalidOid, roleid);

		/*
		 * Advance command counter so that later iterations of this loop will
		 * see the changes already made.  This is essential if, for example,
		 * we are trying to drop both a role and one of its direct members ---
		 * we'll get an error if we try to delete the linking pg_auth_members
		 * tuple twice.  (We do not need a CCI between the two delete loops
		 * above, because it's not allowed for a role to directly contain
		 * itself.)
		 */
		CommandCounterIncrement();
	}

	/*
	 * Now we can clean up; but keep locks until commit.
	 */
	table_close(pg_auth_members_rel, NoLock);
	table_close(pg_authid_rel, NoLock);
}

/*
 * Rename role
 */
ObjectAddress
RenameRole(const char *oldname, const char *newname)
{
	HeapTuple	oldtuple,
				newtuple;
	TupleDesc	dsc;
	Relation	rel;
	Datum		datum;
	bool		isnull;
	Datum		repl_val[Natts_pg_authid];
	bool		repl_null[Natts_pg_authid];
	bool		repl_repl[Natts_pg_authid];
	int			i;
	Oid			roleid;
	ObjectAddress address;
	Form_pg_authid authform;

	rel = table_open(AuthIdRelationId, RowExclusiveLock);
	dsc = RelationGetDescr(rel);

	oldtuple = SearchSysCache1(AUTHNAME, CStringGetDatum(oldname));
	if (!HeapTupleIsValid(oldtuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("role \"%s\" does not exist", oldname)));

	/*
	 * XXX Client applications probably store the session user somewhere, so
	 * renaming it could cause confusion.  On the other hand, there may not be
	 * an actual problem besides a little confusion, so think about this and
	 * decide.  Same for SET ROLE ... we don't restrict renaming the current
	 * effective userid, though.
	 */

	authform = (Form_pg_authid) GETSTRUCT(oldtuple);
	roleid = authform->oid;

	if (roleid == GetSessionUserId())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("session user cannot be renamed")));
	if (roleid == GetOuterUserId())
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("current user cannot be renamed")));

	/*
	 * Check that the user is not trying to rename a system role and not
	 * trying to rename a role into the reserved "pg_" namespace.
	 */
	if (IsReservedName(NameStr(authform->rolname)))
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("role name \"%s\" is reserved",
						NameStr(authform->rolname)),
				 errdetail("Role names starting with \"pg_\" are reserved.")));

	if (IsReservedName(newname))
		ereport(ERROR,
				(errcode(ERRCODE_RESERVED_NAME),
				 errmsg("role name \"%s\" is reserved",
						newname),
				 errdetail("Role names starting with \"pg_\" are reserved.")));

	/* Check whether postgres is being renamed. */
	if (roleid == 10 && strcmp(newname, "postgres") != 0)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("cannot rename postgres"),
				 strcmp(oldname, "postgres") != 0 ?
				  errhint("ALTER ROLE %s RENAME TO postgres", oldname) : 0));

	/*
	 * If built with appropriate switch, whine when regression-testing
	 * conventions for role names are violated.
	 */
#ifdef ENFORCE_REGRESSION_TEST_NAME_RESTRICTIONS
	if (strncmp(newname, "regress_", 8) != 0)
		elog(WARNING, "roles created by regression test cases should have names starting with \"regress_\"");
#endif

	/* make sure the new name doesn't exist */
	if (SearchSysCacheExists1(AUTHNAME, CStringGetDatum(newname)))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("role \"%s\" already exists", newname)));

	/*
	 * createrole is enough privilege unless you want to mess with a superuser
	 */
	if (((Form_pg_authid) GETSTRUCT(oldtuple))->rolsuper)
	{
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to rename superusers")));
	}
	else
	{
		if (!have_createrole_privilege())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("permission denied to rename role")));
	}

	/* OK, construct the modified tuple */
	for (i = 0; i < Natts_pg_authid; i++)
		repl_repl[i] = false;

	repl_repl[Anum_pg_authid_rolname - 1] = true;
	repl_val[Anum_pg_authid_rolname - 1] = DirectFunctionCall1(namein,
															   CStringGetDatum(newname));
	repl_null[Anum_pg_authid_rolname - 1] = false;

	datum = heap_getattr(oldtuple, Anum_pg_authid_rolpassword, dsc, &isnull);

	if (!isnull && get_password_type(TextDatumGetCString(datum)) == PASSWORD_TYPE_MD5)
	{
		/* MD5 uses the username as salt, so just clear it on a rename */
		repl_repl[Anum_pg_authid_rolpassword - 1] = true;
		repl_null[Anum_pg_authid_rolpassword - 1] = true;

		ereport(NOTICE,
				(errmsg("MD5 password cleared because of role rename")));
	}

	newtuple = heap_modify_tuple(oldtuple, dsc, repl_val, repl_null, repl_repl);
	CatalogTupleUpdate(rel, &oldtuple->t_self, newtuple);

	InvokeObjectPostAlterHook(AuthIdRelationId, roleid, 0);

	ObjectAddressSet(address, AuthIdRelationId, roleid);

	ReleaseSysCache(oldtuple);

	/*
	 * Close pg_authid, but keep lock till commit.
	 */
	table_close(rel, NoLock);

	return address;
}

/*
 * GrantRoleStmt
 *
 * Grant/Revoke roles to/from roles
 */
void
GrantRole(GrantRoleStmt *stmt)
{
	Relation	pg_authid_rel;
	Oid			grantor;
	List	   *grantee_ids;
	ListCell   *item;

	if (stmt->grantor)
		grantor = get_rolespec_oid(stmt->grantor, false);
	else
		grantor = GetUserId();

	grantee_ids = roleSpecsToIds(stmt->grantee_roles);

	/* AccessShareLock is enough since we aren't modifying pg_authid */
	pg_authid_rel = table_open(AuthIdRelationId, AccessShareLock);

	/*
	 * Step through all of the granted roles and add/remove entries for the
	 * grantees, or, if admin_opt is set, then just add/remove the admin
	 * option.
	 *
	 * Note: Permissions checking is done by AddRoleMems/DelRoleMems
	 */
	foreach(item, stmt->granted_roles)
	{
		AccessPriv *priv = (AccessPriv *) lfirst(item);
		char	   *rolename = priv->priv_name;
		Oid			roleid;

		/* Must reject priv(columns) and ALL PRIVILEGES(columns) */
		if (rolename == NULL || priv->cols != NIL)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_GRANT_OPERATION),
					 errmsg("column names cannot be included in GRANT/REVOKE ROLE")));

		roleid = get_role_oid(rolename, false);
		if (stmt->is_grant)
			AddRoleMems(rolename, roleid,
						stmt->grantee_roles, grantee_ids,
						grantor, stmt->admin_opt);
		else
			DelRoleMems(rolename, roleid,
						stmt->grantee_roles, grantee_ids,
						stmt->admin_opt);
	}

	/*
	 * Close pg_authid, but keep lock till commit.
	 */
	table_close(pg_authid_rel, NoLock);
}

/*
 * DropOwnedObjects
 *
 * Drop the objects owned by a given list of roles.
 */
void
DropOwnedObjects(DropOwnedStmt *stmt)
{
	List	   *role_ids = roleSpecsToIds(stmt->roles);
	ListCell   *cell;

	/* Check privileges */
	foreach(cell, role_ids)
	{
		Oid			roleid = lfirst_oid(cell);

		if (!has_privs_of_role(GetUserId(), roleid))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("permission denied to drop objects")));
	}

	/* Ok, do it */
	shdepDropOwned(role_ids, stmt->behavior);
}

/*
 * ReassignOwnedObjects
 *
 * Give the objects owned by a given list of roles away to another user.
 */
void
ReassignOwnedObjects(ReassignOwnedStmt *stmt)
{
	List	   *role_ids = roleSpecsToIds(stmt->roles);
	ListCell   *cell;
	Oid			newrole;

	/* Check privileges */
	foreach(cell, role_ids)
	{
		Oid			roleid = lfirst_oid(cell);

		if (!has_privs_of_role(GetUserId(), roleid) &&
			!IsYbDbAdminUser(GetUserId()))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("permission denied to reassign objects")));

		if (superuser_arg(roleid) && !superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("non-superuser cannot reassign objects "
							"from superuser")));
	}

	/* Must have privileges on the receiving side too */
	newrole = get_rolespec_oid(stmt->newrole, false);

	if (!has_privs_of_role(GetUserId(), newrole) &&
		!IsYbDbAdminUser(GetUserId()))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to reassign objects")));

	/* Ok, do it */
	shdepReassignOwned(role_ids, newrole);
}

/*
 * roleSpecsToIds
 *
 * Given a list of RoleSpecs, generate a list of role OIDs in the same order.
 *
 * ROLESPEC_PUBLIC is not allowed.
 */
List *
roleSpecsToIds(List *memberNames)
{
	List	   *result = NIL;
	ListCell   *l;

	foreach(l, memberNames)
	{
		RoleSpec   *rolespec = lfirst_node(RoleSpec, l);
		Oid			roleid;

		roleid = get_rolespec_oid(rolespec, false);
		result = lappend_oid(result, roleid);
	}
	return result;
}

/*
 * AddRoleMems -- Add given members to the specified role
 *
 * rolename: name of role to add to (used only for error messages)
 * roleid: OID of role to add to
 * memberSpecs: list of RoleSpec of roles to add (used only for error messages)
 * memberIds: OIDs of roles to add
 * grantorId: who is granting the membership
 * admin_opt: granting admin option?
 */
static void
AddRoleMems(const char *rolename, Oid roleid,
			List *memberSpecs, List *memberIds,
			Oid grantorId, bool admin_opt)
{
	Relation	pg_authmem_rel;
	TupleDesc	pg_authmem_dsc;
	ListCell   *specitem;
	ListCell   *iditem;

	Assert(list_length(memberSpecs) == list_length(memberIds));

	/* Skip permission check if nothing to do */
	if (!memberIds)
		return;

	/*
	 * Check permissions: must have createrole or admin option on the role to
	 * be changed.  To mess with a superuser role, you gotta be superuser.
	 */
	if (superuser_arg(roleid))
	{
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to alter superusers")));
	}
	else
	{
		if (!have_createrole_privilege() &&
			!is_admin_of_role(grantorId, roleid))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must have admin option on role \"%s\"",
							rolename)));
	}

	/*
	 * The charter of pg_database_owner is to have exactly one, implicit,
	 * situation-dependent member.  There's no technical need for this
	 * restriction.  (One could lift it and take the further step of making
	 * pg_database_ownercheck() equivalent to has_privs_of_role(roleid,
	 * ROLE_PG_DATABASE_OWNER), in which case explicit, situation-independent
	 * members could act as the owner of any database.)
	 */
	if (roleid == ROLE_PG_DATABASE_OWNER)
		ereport(ERROR,
				errmsg("role \"%s\" cannot have explicit members", rolename));

	/*
	 * The role membership grantor of record has little significance at
	 * present.  Nonetheless, inasmuch as users might look to it for a crude
	 * audit trail, let only superusers impute the grant to a third party.
	 */
	if (grantorId != GetUserId() && !superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to set grantor")));

	if (!superuser() && *YBCGetGFlags()->ysql_block_dangerous_roles &&
		(roleid == ROLE_PG_EXECUTE_SERVER_PROGRAM ||
		 roleid == ROLE_PG_READ_ALL_DATA ||
		 roleid == ROLE_PG_READ_SERVER_FILES ||
		 roleid == ROLE_PG_WRITE_ALL_DATA ||
		 roleid == ROLE_PG_WRITE_SERVER_FILES))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("read/write data/files roles are disabled")));

	pg_authmem_rel = table_open(AuthMemRelationId, RowExclusiveLock);
	pg_authmem_dsc = RelationGetDescr(pg_authmem_rel);

	forboth(specitem, memberSpecs, iditem, memberIds)
	{
		RoleSpec   *memberRole = lfirst_node(RoleSpec, specitem);
		Oid			memberid = lfirst_oid(iditem);
		HeapTuple	authmem_tuple;
		HeapTuple	tuple;
		Datum		new_record[Natts_pg_auth_members];
		bool		new_record_nulls[Natts_pg_auth_members];
		bool		new_record_repl[Natts_pg_auth_members];

		/*
		 * pg_database_owner is never a role member.  Lifting this restriction
		 * would require a policy decision about membership loops.  One could
		 * prevent loops, which would include making "ALTER DATABASE x OWNER
		 * TO proposed_datdba" fail if is_member_of_role(pg_database_owner,
		 * proposed_datdba).  Hence, gaining a membership could reduce what a
		 * role could do.  Alternately, one could allow these memberships to
		 * complete loops.  A role could then have actual WITH ADMIN OPTION on
		 * itself, prompting a decision about is_admin_of_role() treatment of
		 * the case.
		 *
		 * Lifting this restriction also has policy implications for ownership
		 * of shared objects (databases and tablespaces).  We allow such
		 * ownership, but we might find cause to ban it in the future.
		 * Designing such a ban would more troublesome if the design had to
		 * address pg_database_owner being a member of role FOO that owns a
		 * shared object.  (The effect of such ownership is that any owner of
		 * another database can act as the owner of affected shared objects.)
		 */
		if (memberid == ROLE_PG_DATABASE_OWNER)
			ereport(ERROR,
					errmsg("role \"%s\" cannot be a member of any role",
						   get_rolespec_name(memberRole)));

		/*
		 * Refuse creation of membership loops, including the trivial case
		 * where a role is made a member of itself.  We do this by checking to
		 * see if the target role is already a member of the proposed member
		 * role.  We have to ignore possible superuserness, however, else we
		 * could never grant membership in a superuser-privileged role.
		 */
		if (is_member_of_role_nosuper(roleid, memberid))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_GRANT_OPERATION),
					 errmsg("role \"%s\" is a member of role \"%s\"",
							rolename, get_rolespec_name(memberRole))));

		/*
		 * Check if entry for this role/member already exists; if so, give
		 * warning unless we are adding admin option.
		 */
		authmem_tuple = SearchSysCache2(AUTHMEMROLEMEM,
										ObjectIdGetDatum(roleid),
										ObjectIdGetDatum(memberid));
		if (HeapTupleIsValid(authmem_tuple) &&
			(!admin_opt ||
			 ((Form_pg_auth_members) GETSTRUCT(authmem_tuple))->admin_option))
		{
			ereport(NOTICE,
					(errmsg("role \"%s\" is already a member of role \"%s\"",
							get_rolespec_name(memberRole), rolename)));
			ReleaseSysCache(authmem_tuple);
			continue;
		}

		/* Build a tuple to insert or update */
		MemSet(new_record, 0, sizeof(new_record));
		MemSet(new_record_nulls, false, sizeof(new_record_nulls));
		MemSet(new_record_repl, false, sizeof(new_record_repl));

		new_record[Anum_pg_auth_members_roleid - 1] = ObjectIdGetDatum(roleid);
		new_record[Anum_pg_auth_members_member - 1] = ObjectIdGetDatum(memberid);
		new_record[Anum_pg_auth_members_grantor - 1] = ObjectIdGetDatum(grantorId);
		new_record[Anum_pg_auth_members_admin_option - 1] = BoolGetDatum(admin_opt);

		if (HeapTupleIsValid(authmem_tuple))
		{
			new_record_repl[Anum_pg_auth_members_grantor - 1] = true;
			new_record_repl[Anum_pg_auth_members_admin_option - 1] = true;
			tuple = heap_modify_tuple(authmem_tuple, pg_authmem_dsc,
									  new_record,
									  new_record_nulls, new_record_repl);
			CatalogTupleUpdate(pg_authmem_rel, &tuple->t_self, tuple);
			ReleaseSysCache(authmem_tuple);
		}
		else
		{
			tuple = heap_form_tuple(pg_authmem_dsc,
									new_record, new_record_nulls);
			CatalogTupleInsert(pg_authmem_rel, tuple);
		}

		/* CCI after each change, in case there are duplicates in list */
		CommandCounterIncrement();
	}

	/*
	 * Close pg_authmem, but keep lock till commit.
	 */
	table_close(pg_authmem_rel, NoLock);
}

/*
 * DelRoleMems -- Remove given members from the specified role
 *
 * rolename: name of role to del from (used only for error messages)
 * roleid: OID of role to del from
 * memberSpecs: list of RoleSpec of roles to del (used only for error messages)
 * memberIds: OIDs of roles to del
 * admin_opt: remove admin option only?
 */
static void
DelRoleMems(const char *rolename, Oid roleid,
			List *memberSpecs, List *memberIds,
			bool admin_opt)
{
	Relation	pg_authmem_rel;
	TupleDesc	pg_authmem_dsc;
	ListCell   *specitem;
	ListCell   *iditem;

	Assert(list_length(memberSpecs) == list_length(memberIds));

	/* Skip permission check if nothing to do */
	if (!memberIds)
		return;

	/*
	 * Check permissions: must have createrole or admin option on the role to
	 * be changed.  To mess with a superuser role, you gotta be superuser.
	 */
	if (superuser_arg(roleid))
	{
		if (!superuser())
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must be superuser to alter superusers")));
	}
	else
	{
		if (!have_createrole_privilege() &&
			!is_admin_of_role(GetUserId(), roleid))
			ereport(ERROR,
					(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
					 errmsg("must have admin option on role \"%s\"",
							rolename)));
	}

	pg_authmem_rel = table_open(AuthMemRelationId, RowExclusiveLock);
	pg_authmem_dsc = RelationGetDescr(pg_authmem_rel);

	forboth(specitem, memberSpecs, iditem, memberIds)
	{
		RoleSpec   *memberRole = lfirst(specitem);
		Oid			memberid = lfirst_oid(iditem);
		HeapTuple	authmem_tuple;

		/*
		 * Find entry for this role/member
		 */
		authmem_tuple = SearchSysCache2(AUTHMEMROLEMEM,
										ObjectIdGetDatum(roleid),
										ObjectIdGetDatum(memberid));
		if (!HeapTupleIsValid(authmem_tuple))
		{
			ereport(WARNING,
					(errmsg("role \"%s\" is not a member of role \"%s\"",
							get_rolespec_name(memberRole), rolename)));
			continue;
		}

		if (!admin_opt)
		{
			/* Remove the entry altogether */
			CatalogTupleDelete(pg_authmem_rel, authmem_tuple);
		}
		else
		{
			/* Just turn off the admin option */
			HeapTuple	tuple;
			Datum		new_record[Natts_pg_auth_members];
			bool		new_record_nulls[Natts_pg_auth_members];
			bool		new_record_repl[Natts_pg_auth_members];

			/* Build a tuple to update with */
			MemSet(new_record, 0, sizeof(new_record));
			MemSet(new_record_nulls, false, sizeof(new_record_nulls));
			MemSet(new_record_repl, false, sizeof(new_record_repl));

			new_record[Anum_pg_auth_members_admin_option - 1] = BoolGetDatum(false);
			new_record_repl[Anum_pg_auth_members_admin_option - 1] = true;

			tuple = heap_modify_tuple(authmem_tuple, pg_authmem_dsc,
									  new_record,
									  new_record_nulls, new_record_repl);
			CatalogTupleUpdate(pg_authmem_rel, &tuple->t_self, tuple);
		}

		ReleaseSysCache(authmem_tuple);

		/* CCI after each change, in case there are duplicates in list */
		CommandCounterIncrement();
	}

	/*
	 * Close pg_authmem, but keep lock till commit.
	 */
	table_close(pg_authmem_rel, NoLock);
}
