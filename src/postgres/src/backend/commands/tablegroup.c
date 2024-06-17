// tablegroup.c
//	  Commands to manipulate table groups.
//	  Tablegroups are used to create colocation groups for tables.
//
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#include "postgres.h"

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "access/heapam.h"
#include "access/reloptions.h"
#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "access/xloginsert.h"
#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_yb_tablegroup.h"
#include "commands/comment.h"
#include "commands/seclabel.h"
#include "commands/tablecmds.h"
#include "commands/tablegroup.h"
#include "commands/tablespace.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/ybccmds.h"
#include "common/file_perm.h"
#include "miscadmin.h"
#include "postmaster/bgwriter.h"
#include "storage/fd.h"
#include "storage/lmgr.h"
#include "storage/standby.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/tqual.h"
#include "utils/varlena.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

Oid binary_upgrade_next_tablegroup_oid = InvalidOid;

/*
 * Create a table group.
 */
Oid
CreateTableGroup(CreateTableGroupStmt *stmt)
{
	Relation	rel;
	Datum		values[Natts_pg_yb_tablegroup];
	bool		nulls[Natts_pg_yb_tablegroup];
	HeapTuple	tuple;
	Oid			tablegroupoid;
	Oid			owneroid;
	Oid			tablespaceoid;
	Acl		   *grpacl = NULL;

	if (!YbTablegroupCatalogExists)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Tablegroup system catalog does not exist.")));
	}

	/*
	 * If not superuser check privileges.
	 * Skip the check for implicitly created tablegroup in a colocated database.
	 */
	if (!stmt->implicit && !superuser())
	{
		AclResult aclresult;
		// Check that user has create privs on the database to allow creation
		// of a new tablegroup.
		aclresult = pg_database_aclcheck(MyDatabaseId, GetUserId(), ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_DATABASE,
						   get_database_name(MyDatabaseId));
	}

	/*
	 * Disallow users from creating tablegroups in a colocated database.
	 */
	if (MyDatabaseColocated && !stmt->implicit)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot use tablegroups in a colocated database")));

	/*
	 * Check that there is no other tablegroup by this name.
	 */
	if (OidIsValid(get_tablegroup_oid(stmt->tablegroupname, true)))
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_OBJECT),
					 errmsg("tablegroup \"%s\" already exists",
					 		stmt->tablegroupname)));

	if (stmt->owner)
		owneroid = get_rolespec_oid(stmt->owner, false);
	else
		owneroid = GetUserId();

	if (stmt->tablespacename)
		tablespaceoid = get_tablespace_oid(stmt->tablespacename, false);
	else
		tablespaceoid = GetDefaultTablespace(RELPERSISTENCE_PERMANENT);

	if (tablespaceoid == GLOBALTABLESPACE_OID)
		/* In all cases disallow placing user relations in pg_global */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("tablegroups cannot be placed in pg_global tablespace")));

	/*
	 * Insert tuple into pg_tablegroup.
	 */
	rel = heap_open(YbTablegroupRelationId, RowExclusiveLock);

	MemSet(nulls, false, sizeof(nulls));

	values[Anum_pg_yb_tablegroup_grpname - 1] =
		DirectFunctionCall1(namein, CStringGetDatum(stmt->tablegroupname));
	values[Anum_pg_yb_tablegroup_grpowner - 1] = ObjectIdGetDatum(owneroid);

	/* Get default permissions and set up grpacl */
	grpacl = get_user_default_acl(OBJECT_YBTABLEGROUP, owneroid, InvalidOid);
	if (grpacl != NULL)
		values[Anum_pg_yb_tablegroup_grpacl - 1] = PointerGetDatum(grpacl);
	else
		nulls[Anum_pg_yb_tablegroup_grpacl - 1] = true;

	/* Set tablegroup tablespace oid */
	values[Anum_pg_yb_tablegroup_grptablespace - 1] = tablespaceoid;

	/* Generate new proposed grpoptions (text array) */
	/* For now no grpoptions. Will be part of Interleaved */

	nulls[Anum_pg_yb_tablegroup_grpoptions - 1] = true;

	tuple = heap_form_tuple(rel->rd_att, values, nulls);

	/*
	 * If YB binary restore mode is set, we want to use the specified tablegroup
	 * oid stored in binary_upgrade_next_tablegroup_oid instead of generating
	 * an oid when inserting the tuple into pg_yb_tablegroup catalog.
	 * YB binary restore mode is similar to PG binary upgrade mode. However, in
	 * YB binary restore mode, we only expecte oids of few types of DB objects
	 * (tablegroup, type, etc) to be set.
	 */
	if (yb_binary_restore)
	{
		/*
		 * The reason to comment out the check below is mainly for supporting
		 * restoring backup of a legacy colocated database to a colocation
		 * database.
		 * We set the YB binary restore mode in ysql_dump for all YB backups.
		 * Since no legacy colocated database contains tablegroup objects,
		 * the backup dumpfile of a legacy colocated database doesn't contain
		 * SQL function calls (binary_upgrade_set_next_tablegroup_oid) to
		 * preserver tablegroup oids.
		 * However, when restoring the dumpfile of a legacy colocated database
		 * to a colocation database, the first CREATE TABLE statement creates
		 * an implicit tablegroup (Colocation GA behaviors).
		 * Since the YB binary restore mode is set, a preserved next tablegroup
		 * oid is expected when creating the tablegroup. Then, the check below
		 * would fail.
		 * As long as we remember to preserve all tablegroup oids,
		 * it is ok to comment out the check as it is purely used as a sanity
		 * check to make sure we preserve tablegroup oids in backup dumpfiles
		 * when we have tablegroup objects.
		 *
		 * TODO: uncomment the check when all customers use colocation, and no
		 * backup from any legacy colocated database is needed to be restored
		 * to a colocation database.
		 */

		/*
		 *	if (!OidIsValid(binary_upgrade_next_tablegroup_oid))
		 *		ereport(ERROR,
		 *				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		 *				errmsg("pg_yb_tablegroup OID value not set when in binary upgrade mode")));
		 */
		if (OidIsValid(binary_upgrade_next_tablegroup_oid))
		{
			HeapTupleSetOid(tuple, binary_upgrade_next_tablegroup_oid);
			binary_upgrade_next_tablegroup_oid = InvalidOid;
		}
	}

	tablegroupoid = CatalogTupleInsert(rel, tuple);

	heap_freetuple(tuple);

	if (IsYugaByteEnabled())
	{
		YBCCreateTablegroup(tablegroupoid, tablespaceoid);
	}

	if (tablespaceoid != InvalidOid)
		recordDependencyOnTablespace(YbTablegroupRelationId, tablegroupoid,
									 tablespaceoid);
	recordDependencyOnOwner(YbTablegroupRelationId, tablegroupoid, owneroid);

	/* We keep the lock on pg_yb_tablegroup until commit */
	heap_close(rel, NoLock);

	return tablegroupoid;
}

/*
 * get_tablegroup_oid - given a tablegroup name, look up the OID
 *
 * If missing_ok is false, throw an error if tablegroup name not found.
 * If true, just return InvalidOid.
 */
Oid
get_tablegroup_oid(const char *tablegroupname, bool missing_ok)
{
	Oid				result;
	Relation		rel;
	HeapScanDesc	scandesc;
	HeapTuple		tuple;
	ScanKeyData		entry[1];

	if (!YbTablegroupCatalogExists)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Tablegroup system catalog does not exist.")));
	}

	/*
	 * Search pg_yb_tablegroup.  We use a heapscan here even though there is an
	 * index on name, on the theory that pg_yb_tablegroup will usually have just
	 * a few entries and so an indexed lookup is a waste of effort.
	 */
	rel = heap_open(YbTablegroupRelationId, AccessShareLock);

	ScanKeyInit(&entry[0],
				Anum_pg_yb_tablegroup_grpname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(tablegroupname));
	scandesc = heap_beginscan_catalog(rel, 1, entry);
	tuple = heap_getnext(scandesc, ForwardScanDirection);

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(tuple))
		result = HeapTupleGetOid(tuple);
	else
		result = InvalidOid;

	heap_endscan(scandesc);
	heap_close(rel, AccessShareLock);

	if (!OidIsValid(result) && !missing_ok)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("tablegroup \"%s\" does not exist",
				 		tablegroupname)));

	return result;
}

/*
 * get_tablegroup_name - given a tablegroup OID, look up the name
 *
 * Returns a palloc'd string, or NULL if no such tablegroup.
 */
char *
get_tablegroup_name(Oid grp_oid)
{
	char		   *result;
	HeapTuple		tuple;

	if (!YbTablegroupCatalogExists)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Tablegroup system catalog does not exist.")));
	}

	/*
	 * Search pg_yb_tablegroup using a cache lookup.
	 */
	tuple = SearchSysCache1(YBTABLEGROUPOID, ObjectIdGetDatum(grp_oid));

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(tuple))
	{
		result = pstrdup(NameStr(((Form_pg_yb_tablegroup) GETSTRUCT(tuple))->grpname));
		ReleaseSysCache(tuple);
	}
	else
		result = NULL;

	return result;
}

/*
 * RemoveTablegroupById - remove a tablegroup by its OID.
 * If a tablegroup does not exist with the provided oid, then an error is raised.
 *
 * grp_oid - the oid of the tablegroup.
 */
void
RemoveTablegroupById(Oid grp_oid, bool remove_implicit)
{
	Relation		pg_tblgrp_rel;
	HeapScanDesc	scandesc;
	ScanKeyData		skey[1];
	HeapTuple		tuple;

	if (!YbTablegroupCatalogExists)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Tablegroup system catalog does not exist.")));
	}

	/*
	 * Checking if it is an implicit tablegroup before checking if the
	 * tablegroup exists to give proper error message if DROP TABLEGROUP ...
	 * CASCADE is executed. This is because during DROP CASCADE, the implicit
	 * tablegroup will be implicitly deleted when dependent tables are dropped
	 * (if ysql_enable_colocated_tables_with_tablespaces is enabled) and we
	 * would get a "tablegroup with oid does not exist" error message.
	 */
	if (MyDatabaseColocated && !remove_implicit)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot drop an implicit tablegroup "
						"in a colocated database.")));
	}

	pg_tblgrp_rel = heap_open(YbTablegroupRelationId, RowExclusiveLock);

	/*
	 * Find the tablegroup to delete.
	 */
	ScanKeyInit(&skey[0],
				ObjectIdAttributeNumber,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(grp_oid));
	scandesc = heap_beginscan_catalog(pg_tblgrp_rel, 1, skey);
	tuple = heap_getnext(scandesc, ForwardScanDirection);

	/* If the tablegroup exists, then remove it, otherwise raise an error. */
	if (!HeapTupleIsValid(tuple))
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("tablegroup with oid %u does not exist",
				 		grp_oid)));
	}

	/* DROP hook for the tablegroup being removed */
	InvokeObjectDropHook(YbTablegroupRelationId, grp_oid, 0);

	/*
	 * Remove the pg_yb_tablegroup tuple
	 */
	CatalogTupleDelete(pg_tblgrp_rel, tuple);

	heap_endscan(scandesc);

	if (IsYugaByteEnabled())
	{
		YBCDropTablegroup(grp_oid);
	}

	/* We keep the lock on pg_yb_tablegroup until commit */
	heap_close(pg_tblgrp_rel, NoLock);
}

char*
get_implicit_tablegroup_name(Oid oidSuffix)
{
	char *tablegroup_name_from_tablespace = (char*) palloc(
		(
			10 /*strlen("colocation")*/ + 10 /*Max digits in OID*/ + 1 /*Under Scores*/
			+ 1 /*Null Terminator*/
		) * sizeof(char)
	);

	sprintf(tablegroup_name_from_tablespace, "colocation_%u", oidSuffix);
	return tablegroup_name_from_tablespace;
}

/*
 * Rename tablegroup
 */
ObjectAddress
RenameTablegroup(const char *oldname, const char *newname)
{
	Oid				tablegroupoid;
	HeapTuple		newtup;
	Relation		rel;
	ObjectAddress	address;
	HeapTuple		tuple;
	HeapScanDesc	scandesc;
	ScanKeyData		entry[1];

	if (!YbTablegroupCatalogExists) {
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Tablegroup system catalog does not exist.")));
	}

	/*
	 * Look up the target tablegroup's OID, and get exclusive lock on it. We
	 * need this for the same reasons as DROP TABLEGROUP.
	 */
	rel = heap_open(YbTablegroupRelationId, RowExclusiveLock);
	ScanKeyInit(&entry[0],
				Anum_pg_yb_tablegroup_grpname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(oldname));
	scandesc = heap_beginscan_catalog(rel, 1, entry);
	tuple = heap_getnext(scandesc, ForwardScanDirection);
	heap_endscan(scandesc);

	if (!HeapTupleIsValid(tuple))
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("tablegroup \"%s\" does not exist",
				 oldname)));
	}

	tablegroupoid = HeapTupleGetOid(tuple);

	/* must be owner or superuser */
	if (!superuser() && !pg_tablegroup_ownercheck(tablegroupoid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_YBTABLEGROUP, oldname);

	/*
	 * Make sure the new name doesn't exist.
	 */
	if (OidIsValid(get_tablegroup_oid(newname, true)))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("tablegroup \"%s\" already exists", newname)));

	/* rename */
	newtup = SearchSysCacheCopy1(YBTABLEGROUPOID, ObjectIdGetDatum(tablegroupoid));
	if (!HeapTupleIsValid(newtup))
		elog(ERROR, "cache lookup failed for tablegroup %u", tablegroupoid);

	namestrcpy(&(((Form_pg_yb_tablegroup) GETSTRUCT(newtup))->grpname), newname);
	CatalogTupleUpdate(rel, &newtup->t_self, newtup);

	InvokeObjectPostAlterHook(YbTablegroupRelationId, tablegroupoid, 0);

	ObjectAddressSet(address, YbTablegroupRelationId, tablegroupoid);

	heap_freetuple(newtup);

	/*
	 * Close pg_yb_tablegroup, but keep lock till commit.
	 */
	heap_close(rel, NoLock);

	return address;
}

/*
 * ALTER TABLEGROUP name OWNER TO newowner
 */
ObjectAddress
AlterTablegroupOwner(const char *grpname, Oid newOwnerId)
{
	Oid					tablegroupoid;
	HeapTuple			tuple;
	Relation			rel;
	ScanKeyData			entry[1];
	HeapScanDesc		scandesc;
	Form_pg_yb_tablegroup	datForm;
	ObjectAddress		address;

	if (!YbTablegroupCatalogExists) {
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Tablegroup system catalog does not exist.")));
	}

	rel = heap_open(YbTablegroupRelationId, RowExclusiveLock);
	ScanKeyInit(&entry[0],
				Anum_pg_yb_tablegroup_grpname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(grpname));
	scandesc = heap_beginscan_catalog(rel, 1, entry);
	tuple = heap_getnext(scandesc, ForwardScanDirection);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("tablegroup \"%s\" does not exist", grpname)));

	tablegroupoid = HeapTupleGetOid(tuple);
	datForm = (Form_pg_yb_tablegroup) GETSTRUCT(tuple);

	/*
	 * If the new owner is the same as the existing owner, consider the
	 * command to have succeeded.  This is to be consistent with other
	 * objects.
	 */
	if (datForm->grpowner != newOwnerId)
	{
		Datum		repl_val[Natts_pg_yb_tablegroup];
		bool		repl_null[Natts_pg_yb_tablegroup];
		bool		repl_repl[Natts_pg_yb_tablegroup];
		Acl		   *newAcl;
		Datum		aclDatum;
		bool		isNull;
		HeapTuple	newtuple;

		/* Otherwise, must be owner of the existing object or a superuser */
		if (!superuser() && !pg_tablegroup_ownercheck(tablegroupoid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_YBTABLEGROUP, grpname);

		/* Must be able to become new owner */
		check_is_member_of_role(GetUserId(), newOwnerId);

		memset(repl_null, false, sizeof(repl_null));
		memset(repl_repl, false, sizeof(repl_repl));

		repl_repl[Anum_pg_yb_tablegroup_grpowner - 1] = true;
		repl_val[Anum_pg_yb_tablegroup_grpowner - 1] = ObjectIdGetDatum(newOwnerId);

		/*
		 * Determine the modified ACL for the new owner.  This is only
		 * necessary when the ACL is non-null.
		 */
		aclDatum = heap_getattr(tuple,
								Anum_pg_yb_tablegroup_grpacl,
								RelationGetDescr(rel),
								&isNull);
		if (!isNull)
		{
			newAcl = aclnewowner(DatumGetAclP(aclDatum),
								 datForm->grpowner, newOwnerId);
			repl_repl[Anum_pg_yb_tablegroup_grpacl - 1] = true;
			repl_val[Anum_pg_yb_tablegroup_grpacl - 1] = PointerGetDatum(newAcl);
		}

		newtuple = heap_modify_tuple(tuple, RelationGetDescr(rel), repl_val, repl_null, repl_repl);
		CatalogTupleUpdate(rel, &newtuple->t_self, newtuple);

		changeDependencyOnOwner(YbTablegroupRelationId, tablegroupoid, newOwnerId);

		heap_freetuple(newtuple);

	}

	InvokeObjectPostAlterHook(YbTablegroupRelationId, tablegroupoid, 0);

	ObjectAddressSet(address, YbTablegroupRelationId, tablegroupoid);

	heap_endscan(scandesc);

	/* Close pg_yb_tablegroup, but keep lock till commit */
	heap_close(rel, NoLock);

	return address;
}

/*
 * ybAlterTablespaceForTablegroup - Update tablespace entry
 *									for the given tablegroup.
 *
 * If a tablegroup does not exist with the provided oid, then an error is
 * raised.
 */
void
ybAlterTablespaceForTablegroup(const char *grpname, Oid newTablespace)
{
	Oid					tablegroupoid;
	HeapTuple			tuple;
	Relation			rel;
	ScanKeyData			entry[1];
	HeapScanDesc		scandesc;
	Form_pg_yb_tablegroup	datForm;

	if (!YbTablegroupCatalogExists)
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("Tablegroup system catalog does not exist.")));
	}

	rel = heap_open(YbTablegroupRelationId, RowExclusiveLock);
	ScanKeyInit(&entry[0],
				Anum_pg_yb_tablegroup_grpname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(grpname));
	scandesc = heap_beginscan_catalog(rel, 1, entry);
	tuple = heap_getnext(scandesc, ForwardScanDirection);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("tablegroup \"%s\" does not exist", grpname)));

	tablegroupoid = HeapTupleGetOid(tuple);
	datForm = (Form_pg_yb_tablegroup) GETSTRUCT(tuple);

	if (datForm->grptablespace != newTablespace)
	{
		datForm->grptablespace = newTablespace;

		CatalogTupleUpdate(rel, &tuple->t_self, tuple);
	}

	InvokeObjectPostAlterHook(YbTablegroupRelationId, tablegroupoid, 0);

	heap_endscan(scandesc);

	heap_close(rel, RowExclusiveLock);
}
