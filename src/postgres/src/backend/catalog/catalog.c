/*-------------------------------------------------------------------------
 *
 * catalog.c
 *		routines concerned with catalog naming conventions and other
 *		bits of hard-wired knowledge
 *
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/catalog/catalog.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <fcntl.h>
#include <unistd.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "access/sysattr.h"
#include "access/transam.h"
#include "catalog/catalog.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_pltemplate.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/pg_replication_origin.h"
#include "catalog/pg_shdepend.h"
#include "catalog/pg_shdescription.h"
#include "catalog/pg_shseclabel.h"
#include "catalog/pg_subscription.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_type.h"
#include "catalog/toasting.h"
#include "commands/defrem.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "utils/fmgroids.h"
#include "utils/rel.h"
#include "utils/tqual.h"

/* YB includes. */
#include "access/htup_details.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/pg_yb_profile.h"
#include "catalog/pg_yb_role_profile.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "pg_yb_utils.h"

/*
 * IsSystemRelation
 *		True iff the relation is either a system catalog or toast table.
 *		By a system catalog, we mean one that created in the pg_catalog schema
 *		during initdb.  User-created relations in pg_catalog don't count as
 *		system catalogs.
 *
 *		NB: TOAST relations are considered system relations by this test
 *		for compatibility with the old IsSystemRelationName function.
 *		This is appropriate in many places but not all.  Where it's not,
 *		also check IsToastRelation or use IsCatalogRelation().
 */
bool
IsSystemRelation(Relation relation)
{
	return IsSystemClass(RelationGetRelid(relation), relation->rd_rel);
}

/*
 * IsSystemClass
 *		Like the above, but takes a Form_pg_class as argument.
 *		Used when we do not want to open the relation and have to
 *		search pg_class directly.
 */
bool
IsSystemClass(Oid relid, Form_pg_class reltuple)
{
	return IsToastClass(reltuple) || IsCatalogClass(relid, reltuple);
}

/*
 * IsCatalogRelation
 *		True iff the relation is a system catalog, or the toast table for
 *		a system catalog.  By a system catalog, we mean one that created
 *		in the pg_catalog schema during initdb.  As with IsSystemRelation(),
 *		user-created relations in pg_catalog don't count as system catalogs.
 *
 *		Note that IsSystemRelation() returns true for ALL toast relations,
 *		but this function returns true only for toast relations of system
 *		catalogs.
 */
bool
IsCatalogRelation(Relation relation)
{
	return IsCatalogClass(RelationGetRelid(relation), relation->rd_rel);
}

/*
 * IsCatalogClass
 *		True iff the relation is a system catalog relation.
 *
 * Check IsCatalogRelation() for details.
 */
bool
IsCatalogClass(Oid relid, Form_pg_class reltuple)
{
	Oid			relnamespace = reltuple->relnamespace;

	/*
	 * Never consider relations outside pg_catalog/pg_toast to be catalog
	 * relations.
	 */
	if (!IsSystemNamespace(relnamespace) && !IsToastNamespace(relnamespace))
		return false;

	/* ----
	 * Check whether the oid was assigned during initdb, when creating the
	 * initial template database. Minus the relations in information_schema
	 * excluded above, these are integral part of the system.
	 * We could instead check whether the relation is pinned in pg_depend, but
	 * this is noticeably cheaper and doesn't require catalog access.
	 *
	 * This test is safe since even an oid wraparound will preserve this
	 * property (cf. GetNewObjectId()) and it has the advantage that it works
	 * correctly even if a user decides to create a relation in the pg_catalog
	 * namespace.
	 * ----
	 */
	return relid < FirstNormalObjectId;
}

/*
 * IsToastRelation
 *		True iff relation is a TOAST support relation (or index).
 */
bool
IsToastRelation(Relation relation)
{
	return IsToastNamespace(RelationGetNamespace(relation));
}

/*
 * IsToastClass
 *		Like the above, but takes a Form_pg_class as argument.
 *		Used when we do not want to open the relation and have to
 *		search pg_class directly.
 */
bool
IsToastClass(Form_pg_class reltuple)
{
	Oid			relnamespace = reltuple->relnamespace;

	return IsToastNamespace(relnamespace);
}

/*
 * IsSystemNamespace
 *		True iff namespace is pg_catalog.
 *
 * NOTE: the reason this isn't a macro is to avoid having to include
 * catalog/pg_namespace.h in a lot of places.
 */
bool
IsSystemNamespace(Oid namespaceId)
{
	return namespaceId == PG_CATALOG_NAMESPACE;
}

/*
 * Same logic as with IsSystemNamespace, to be used when OID isn't known.
 * NULL name results in InvalidOid.
 */
bool
YbIsSystemNamespaceByName(const char *namespace_name)
{
	Oid namespace_oid;

	namespace_oid = namespace_name ?
		LookupExplicitNamespace(namespace_name, true) :
		InvalidOid;

	return IsSystemNamespace(namespace_oid);
}

/*
 * IsToastNamespace
 *		True iff namespace is pg_toast or my temporary-toast-table namespace.
 *
 * Note: this will return false for temporary-toast-table namespaces belonging
 * to other backends.  Those are treated the same as other backends' regular
 * temp table namespaces, and access is prevented where appropriate.
 */
bool
IsToastNamespace(Oid namespaceId)
{
	return (namespaceId == PG_TOAST_NAMESPACE) ||
		isTempToastNamespace(namespaceId);
}


/*
 * IsReservedName
 *		True iff name starts with the pg_ prefix.
 *
 *		For some classes of objects, the prefix pg_ is reserved for
 *		system objects only.  As of 8.0, this was only true for
 *		schema and tablespace names.  With 9.6, this is also true
 *		for roles.
 */
bool
IsReservedName(const char *name)
{
	/* ugly coding for speed */
	return (name[0] == 'p' &&
			name[1] == 'g' &&
			name[2] == '_');
}


/*
 * IsSharedRelation
 *		Given the OID of a relation, determine whether it's supposed to be
 *		shared across an entire database cluster.
 *
 * In older releases, this had to be hard-wired so that we could compute the
 * locktag for a relation and lock it before examining its catalog entry.
 * Since we now have MVCC catalog access, the race conditions that made that
 * a hard requirement are gone, so we could look at relaxing this restriction.
 * However, if we scanned the pg_class entry to find relisshared, and only
 * then locked the relation, pg_class could get updated in the meantime,
 * forcing us to scan the relation again, which would definitely be complex
 * and might have undesirable performance consequences.  Fortunately, the set
 * of shared relations is fairly static, so a hand-maintained list of their
 * OIDs isn't completely impractical.
 */
bool
IsSharedRelation(Oid relationId)
{
	/* These are the shared catalogs (look for BKI_SHARED_RELATION) */
	if (relationId == AuthIdRelationId ||
		relationId == AuthMemRelationId ||
		relationId == DatabaseRelationId ||
		relationId == PLTemplateRelationId ||
		relationId == SharedDescriptionRelationId ||
		relationId == SharedDependRelationId ||
		relationId == SharedSecLabelRelationId ||
		relationId == TableSpaceRelationId ||
		relationId == DbRoleSettingRelationId ||
		relationId == ReplicationOriginRelationId ||
		relationId == SubscriptionRelationId ||
		relationId == YBCatalogVersionRelationId ||
		relationId == YbProfileRelationId ||
		relationId == YbRoleProfileRelationId)
		return true;
	/* These are their indexes (see indexing.h) */
	if (relationId == AuthIdRolnameIndexId ||
		relationId == AuthIdOidIndexId ||
		relationId == AuthMemRoleMemIndexId ||
		relationId == AuthMemMemRoleIndexId ||
		relationId == DatabaseNameIndexId ||
		relationId == DatabaseOidIndexId ||
		relationId == PLTemplateNameIndexId ||
		relationId == SharedDescriptionObjIndexId ||
		relationId == SharedDependDependerIndexId ||
		relationId == SharedDependReferenceIndexId ||
		relationId == SharedSecLabelObjectIndexId ||
		relationId == TablespaceOidIndexId ||
		relationId == TablespaceNameIndexId ||
		relationId == DbRoleSettingDatidRolidIndexId ||
		relationId == ReplicationOriginIdentIndex ||
		relationId == ReplicationOriginNameIndex ||
		relationId == SubscriptionObjectIndexId ||
		relationId == SubscriptionNameIndexId ||
		relationId == YBCatalogVersionDbOidIndexId ||
		relationId == YbProfileOidIndexId ||
		relationId == YbProfileRolnameIndexId ||
		relationId == YbRoleProfileOidIndexId)
		return true;
	/* These are their toast tables and toast indexes (see toasting.h) */
	if (relationId == PgShdescriptionToastTable ||
		relationId == PgShdescriptionToastIndex ||
		relationId == PgDbRoleSettingToastTable ||
		relationId == PgDbRoleSettingToastIndex ||
		relationId == PgShseclabelToastTable ||
		relationId == PgShseclabelToastIndex)
		return true;

	/* In test mode, there might be shared relations other than predefined ones. */
	if (yb_test_system_catalogs_creation)
	{
		/* To avoid cycle */
		if (relationId == RelationRelationId)
			return false;

		Relation  pg_class = heap_open(RelationRelationId, AccessShareLock);
		HeapTuple tuple    = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relationId));

		bool result = HeapTupleIsValid(tuple)
			? ((Form_pg_class) GETSTRUCT(tuple))->relisshared
			: false;

		if (HeapTupleIsValid(tuple))
			heap_freetuple(tuple);

		heap_close(pg_class, AccessShareLock);

		return result;
	}

	return false;
}

/*
 * GetBackendOidFromRelPersistence
 *		Returns backend oid for the given type of relation persistence.
 */
Oid
GetBackendOidFromRelPersistence(char relpersistence)
{
	switch (relpersistence)
	{
		case RELPERSISTENCE_TEMP:
			return BackendIdForTempRelations();
		case RELPERSISTENCE_UNLOGGED:
		case RELPERSISTENCE_PERMANENT:
			return InvalidBackendId;
		default:
			elog(ERROR, "invalid relpersistence: %c", relpersistence);
			return InvalidOid;	/* placate compiler */
	}
}

/*
 * DoesRelFileExist
 *		True iff there is an existing file of the same name for this relation.
 */
bool
DoesRelFileExist(const RelFileNodeBackend *rnode)
{
	bool 	collides;
	char 	*rpath = relpath(*rnode, MAIN_FORKNUM);
	int 	fd = BasicOpenFile(rpath, O_RDONLY | PG_BINARY);

	if (fd >= 0)
	{
		/* definite collision */
		close(fd);
		collides = true;
	}
	else
	{
		/*
		 * Here we have a little bit of a dilemma: if errno is something
		 * other than ENOENT, should we declare a collision and loop? In
		 * particular one might think this advisable for, say, EPERM.
		 * However there really shouldn't be any unreadable files in a
		 * tablespace directory, and if the EPERM is actually complaining
		 * that we can't read the directory itself, we'd be in an infinite
		 * loop.  In practice it seems best to go ahead regardless of the
		 * errno.  If there is a colliding file we will get an smgr
		 * failure when we attempt to create the new relation file.
		 */
		collides = false;
	}

	pfree(rpath);
	return collides;
}

/*
 * DoesOidExistInRelation
 *		True iff the oid already exists in the relation.
 *		Used typically with relation = pg_class, to check if a new oid is
 *		already in use.
 */
bool
DoesOidExistInRelation(Oid oid,
					   Relation relation,
					   Oid indexId,
					   AttrNumber oidcolumn)
{
	SysScanDesc scan;
	ScanKeyData key;
	bool		collides;

	ScanKeyInit(&key,
				oidcolumn,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(oid));

	/* see notes in GetNewOid about using SnapshotAny */
	scan = systable_beginscan(relation, indexId, true, SnapshotAny, 1, &key);

	collides = HeapTupleIsValid(systable_getnext(scan));

	systable_endscan(scan);

	return collides;
}

/*
 * GetNewOid
 *		Generate a new OID that is unique within the given relation.
 *
 * Caller must have a suitable lock on the relation.
 *
 * Uniqueness is promised only if the relation has a unique index on OID.
 * This is true for all system catalogs that have OIDs, but might not be
 * true for user tables.  Note that we are effectively assuming that the
 * table has a relatively small number of entries (much less than 2^32)
 * and there aren't very long runs of consecutive existing OIDs.  Again,
 * this is reasonable for system catalogs but less so for user tables.
 *
 * Since the OID is not immediately inserted into the table, there is a
 * race condition here; but a problem could occur only if someone else
 * managed to cycle through 2^32 OIDs and generate the same OID before we
 * finish inserting our row.  This seems unlikely to be a problem.  Note
 * that if we had to *commit* the row to end the race condition, the risk
 * would be rather higher; therefore we use SnapshotAny in the test, so that
 * we will see uncommitted rows.  (We used to use SnapshotDirty, but that has
 * the disadvantage that it ignores recently-deleted rows, creating a risk
 * of transient conflicts for as long as our own MVCC snapshots think a
 * recently-deleted row is live.  The risk is far higher when selecting TOAST
 * OIDs, because SnapshotToast considers dead rows as active indefinitely.)
 */
Oid
GetNewOid(Relation relation)
{
	Oid			oidIndex;

	/* If relation doesn't have OIDs at all, caller is confused */
	Assert(relation->rd_rel->relhasoids);

	if (IsYugaByteEnabled())
	{
		if (relation->rd_rel->relisshared)
			YbDatabaseIdForNewObjectId = TemplateDbOid;
		else
			YbDatabaseIdForNewObjectId = MyDatabaseId;
	}

	/* In bootstrap mode, we don't have any indexes to use */
	if (IsBootstrapProcessingMode())
		return GetNewObjectId();

	/* The relcache will cache the identity of the OID index for us */
	oidIndex = RelationGetOidIndex(relation);

	/* If no OID index, just hand back the next OID counter value */
	if (!OidIsValid(oidIndex))
	{
		/*
		 * In YugaByte we convert the OID index into a primary key.
		 */
		if (!IsYugaByteEnabled())
		{
			/*
			 * System catalogs that have OIDs should *always* have a unique OID
			 * index; we should only take this path for user tables. Give a
			 * warning if it looks like somebody forgot an index.
			 */
			if (IsSystemRelation(relation))
				elog(WARNING,
				     "generating possibly-non-unique OID for \"%s\"",
				     RelationGetRelationName(relation));
		}
		return GetNewObjectId();
	}

	/* Otherwise, use the index to find a nonconflicting OID */
	return GetNewOidWithIndex(relation, oidIndex, ObjectIdAttributeNumber);
}

/*
 * GetNewOidWithIndex
 *		Guts of GetNewOid: use the supplied index
 *
 * This is exported separately because there are cases where we want to use
 * an index that will not be recognized by RelationGetOidIndex: TOAST tables
 * have indexes that are usable, but have multiple columns and are on
 * ordinary columns rather than a true OID column.  This code will work
 * anyway, so long as the OID is the index's first column.  The caller must
 * pass in the actual heap attnum of the OID column, however.
 *
 * Caller must have a suitable lock on the relation.
 */
Oid
GetNewOidWithIndex(Relation relation, Oid indexId, AttrNumber oidcolumn)
{
	Oid			newOid;

	/*
	 * We should never be asked to generate a new pg_type OID during
	 * pg_upgrade; doing so would risk collisions with the OIDs it wants to
	 * assign.  Hitting this assert means there's some path where we failed to
	 * ensure that a type OID is determined by commands in the dump script.
	 */
	Assert(!IsBinaryUpgrade || yb_binary_restore || RelationGetRelid(relation) != TypeRelationId);

	if (IsYugaByteEnabled())
	{
		if (relation->rd_rel->relisshared)
			YbDatabaseIdForNewObjectId = TemplateDbOid;
		else
			YbDatabaseIdForNewObjectId = MyDatabaseId;
	}

	/* Generate new OIDs until we find one not in the table */
	do
	{
		CHECK_FOR_INTERRUPTS();

		newOid = GetNewObjectId();
	} while (DoesOidExistInRelation(newOid, relation, indexId, oidcolumn));

	return newOid;
}

/*
 * GetNewRelFileNode
 *		Generate a new relfilenode number that is unique within the
 *		database of the given tablespace.
 *
 * If the relfilenode will also be used as the relation's OID, pass the
 * opened pg_class catalog, and this routine will guarantee that the result
 * is also an unused OID within pg_class.  If the result is to be used only
 * as a relfilenode for an existing relation, pass NULL for pg_class.
 *
 * As with GetNewOid, there is some theoretical risk of a race condition,
 * but it doesn't seem worth worrying about.
 *
 * Note: we don't support using this in bootstrap mode.  All relations
 * created by bootstrap have preassigned OIDs, so there's no need.
 */
Oid
GetNewRelFileNode(Oid reltablespace, Relation pg_class, char relpersistence)
{
	RelFileNodeBackend rnode;

	/*
	 * If we ever get here during pg_upgrade, there's something wrong; all
	 * relfilenode assignments during a binary-upgrade run should be
	 * determined by commands in the dump script.
	 */
	Assert(!IsBinaryUpgrade || yb_binary_restore);

	/* This logic should match RelationInitPhysicalAddr */
	rnode.node.spcNode = reltablespace ? reltablespace : MyDatabaseTableSpace;
	rnode.node.dbNode = (rnode.node.spcNode == GLOBALTABLESPACE_OID) ? InvalidOid : MyDatabaseId;

	/*
	 * The relpath will vary based on the backend ID, so we must initialize
	 * that properly here to make sure that any collisions based on filename
	 * are properly detected.
	 */
	rnode.backend = GetBackendOidFromRelPersistence(relpersistence);;

	/*
	 * All the shared relations have relfilenode value as 0, which suggests
	 * that relfilenode is only used for non-shared relations. That's why
	 * MyDatabaseId should be used for new relfilenode OID allocation.
	 */
	if (IsYugaByteEnabled())
	{
		Assert(!pg_class || !pg_class->rd_rel->relisshared);
		YbDatabaseIdForNewObjectId = MyDatabaseId;
	}

	do
	{
		CHECK_FOR_INTERRUPTS();

		/* Generate the OID */
		if (pg_class)
			rnode.node.relNode = GetNewOid(pg_class);
		else
			rnode.node.relNode = GetNewObjectId();

		/* Check for existing file of same name */
	} while (DoesRelFileExist(&rnode));

	return rnode.node.relNode;
}

/*
 * IsTableOidUnused
 *		Returns true iff the given table oid is not used by any other table
 *		within the database of the given tablespace.
 *
 * First checks pg_class to see if the oid is in use (similar to
 * GetNewOidWithIndex), and then checks if there are any existing relfiles that
 * have the same oid (similar to GetNewRelFileNode).
 *
 * Similar to GetNewOidWithIndex and GetNewRelFileNode, there is a theoretical
 * race condition, but since we don't worry about it there, it should be fine
 * here as well.
 */
bool
IsTableOidUnused(Oid table_oid,
				 Oid reltablespace,
				 Relation pg_class,
				 char relpersistence)
{
	RelFileNodeBackend rnode;
	Oid				   oidIndex;
	bool			   collides;

	/* First check for if the oid is used in pg_class. */

	/* The relcache will cache the identity of the OID index for us */
	oidIndex = RelationGetOidIndex(pg_class);

	if (!OidIsValid(oidIndex))
	{
		elog(WARNING, "Could not find oid index of pg_class.");
	}

	collides = DoesOidExistInRelation(table_oid,
									  pg_class,
									  oidIndex,
									  ObjectIdAttributeNumber);

	if (!collides)
	{
		/*
		 * Check if there are existing relfiles with the oid.
		 * YB Note: It looks like we only run into collisions here for
		 * 			temporary tables.
		 */

		/*
		 * The relpath will vary based on the backend ID, so we must initialize
		 * that properly here to make sure that any collisions based on filename
		 * are properly detected.
		 */
		rnode.backend = GetBackendOidFromRelPersistence(relpersistence);

		/* This logic should match RelationInitPhysicalAddr */
		rnode.node.spcNode = reltablespace ? reltablespace
										   : MyDatabaseTableSpace;
		rnode.node.dbNode = (rnode.node.spcNode == GLOBALTABLESPACE_OID)
								? InvalidOid
								: MyDatabaseId;

		rnode.node.relNode = table_oid;

		/* Check for existing file of same name */
		collides = DoesRelFileExist(&rnode);
	}

	return !collides;
}

/*
 * GetTableOidFromRelOptions
 *		Scans through relOptions for any 'table_oid' options, and ensures
 *		that oid is available. Returns that oid, or InvalidOid if unspecified.
 */
Oid
GetTableOidFromRelOptions(List *relOptions,
						  Oid reltablespace,
						  char relpersistence)
{
	ListCell   *opt_cell;
	Oid			table_oid;
	bool		is_oid_free;

	foreach(opt_cell, relOptions)
	{
		DefElem *def = (DefElem *) lfirst(opt_cell);
		if (strcmp(def->defname, "table_oid") == 0)
		{
			const char* hintmsg;
			if (!parse_oid(defGetString(def), &table_oid, &hintmsg))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid value for OID option \"table_oid\""),
						 hintmsg ? errhint("%s", _(hintmsg)) : 0));
			if (OidIsValid(table_oid))
			{
				Relation pg_class_desc =
					heap_open(RelationRelationId, RowExclusiveLock);
				is_oid_free = IsTableOidUnused(table_oid,
											   reltablespace,
											   pg_class_desc,
											   relpersistence);
				heap_close(pg_class_desc, RowExclusiveLock);

				if (is_oid_free)
					return table_oid;
				else
					ereport(ERROR,
							(errcode(ERRCODE_DUPLICATE_OBJECT),
							 errmsg("table OID %u is in use", table_oid)));

				/* Only process the first table_oid. */
				break;
			}
		}
	}

	return InvalidOid;
}

/*
 * GetColocationIdFromRelOptions
 *		Scans through relOptions for any 'colocation_id' options.
 *		Returns that ID, or InvalidOid if unspecified.
 *
 * This is only used during table/index creation, as this reloption is not
 * persisted.
 */
Oid
YbGetColocationIdFromRelOptions(List *relOptions)
{
	ListCell   *opt_cell;
	Oid        colocation_id;

	foreach(opt_cell, relOptions)
	{
		DefElem *def = (DefElem *) lfirst(opt_cell);
		if (strcmp(def->defname, "colocation_id") == 0)
		{
			const char* hintmsg;
			if (!parse_oid(defGetString(def), &colocation_id, &hintmsg))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid value for OID option \"colocation_id\""),
						 hintmsg ? errhint("%s", _(hintmsg)) : 0));
			if (OidIsValid(colocation_id))
				return colocation_id;
		}
	}

	return InvalidOid;
}

/*
 * GetRowTypeOidFromRelOptions
 *		Scans through relOptions for any 'row_type_oid' options, and ensures
 *		that oid is available. Returns that oid, or InvalidOid if unspecified.
 */
Oid
GetRowTypeOidFromRelOptions(List *relOptions)
{
	ListCell  *opt_cell;
	Oid       row_type_oid;
	Relation  pg_type_desc;
	HeapTuple tuple;

	foreach(opt_cell, relOptions)
	{
		DefElem *def = (DefElem *) lfirst(opt_cell);
		if (strcmp(def->defname, "row_type_oid") == 0)
		{
			const char* hintmsg;
			if (!parse_oid(defGetString(def), &row_type_oid, &hintmsg))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("invalid value for OID option \"row_type_oid\""),
						 hintmsg ? errhint("%s", _(hintmsg)) : 0));
			if (OidIsValid(row_type_oid))
			{
				pg_type_desc = heap_open(TypeRelationId, AccessExclusiveLock);

				tuple = SearchSysCacheCopy1(TYPEOID, ObjectIdGetDatum(row_type_oid));
				if (HeapTupleIsValid(tuple))
					ereport(ERROR,
							(errcode(ERRCODE_DUPLICATE_OBJECT),
							 errmsg("type OID %u is in use", row_type_oid)));

				heap_close(pg_type_desc, AccessExclusiveLock);

				return row_type_oid;
			}
		}
	}

	return InvalidOid;
}

bool
YbGetUseInitdbAclFromRelOptions(List *options)
{
	ListCell  *opt_cell;

	foreach(opt_cell, options)
	{
		// Don't care about multiple occurrences, this reloption is internal.
		DefElem *def = lfirst_node(DefElem, opt_cell);
		if (strcmp(def->defname, "use_initdb_acl") == 0)
			return defGetBoolean(def);
	}

	return InvalidOid;
}

/*
 * Is this relation stored into the YB system catalog tablet?
 */
bool
YbIsSysCatalogTabletRelation(Relation rel)
{
	Oid			namespaceId = RelationGetNamespace(rel);
	char	   *namespace_name = get_namespace_name_or_temp(namespaceId);

	return YbIsSysCatalogTabletRelationByIds(RelationGetRelid(rel),
											 namespaceId,
											 namespace_name);
}

/*
 * Same as above but potentially faster by avoiding get_namespace_name_or_temp
 * call in case the caller already has the namespace name.
 */
bool
YbIsSysCatalogTabletRelationByIds(Oid relationId, Oid namespaceId,
								  char *namespace_name)
{
	Assert(namespace_name);
	/* Re-evaluate this when toast relations are supported. */
	Assert(!IsToastNamespace(namespaceId));

	/*
	 * YB puts catalog relations and information_schema relations in the sys
	 * catalog tablet.  From commit 5c28dc4a654cc246d0da0807e18d07b81cfe45eb
	 * to the time of writing (2024-05-22), it is not possible to create user
	 * relations in pg_catalog because it hits an error if IsYsqlUpgrade is
	 * false, and if that is true, then relations are created as system
	 * relations.  Before that commit, it seems to segfault when attempting to
	 * create table in pg_catalog (at least when testing on v2.0.11.0).
	 */
	if (IsSystemNamespace(namespaceId) ||
		strcmp(namespace_name, "information_schema") == 0)
	{
		if (relationId >= FirstNormalObjectId)
		{
			/*
			 * At the time of writing (2024-05-22), it is possible for users
			 * (with the correct privileges) to create user relations in
			 * information_schema.  Since these are persisted in the sys
			 * catalog tablet and this function may be called with such tables,
			 * don't hard fail upon encountering such tables.
			 */
			Assert(strcmp(namespace_name, "information_schema") == 0);
			ereport(WARNING,
					(errmsg("unexpected user relation in system namespace"),
					 errdetail("Table with oid %u should not be in namespace"
							   " %s.",
							   relationId, namespace_name)));
		}
		return true;
	}
	return false;
}
