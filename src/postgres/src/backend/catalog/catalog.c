/*-------------------------------------------------------------------------
 *
 * catalog.c
 *		routines concerned with catalog naming conventions and other
 *		bits of hard-wired knowledge
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
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
#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/table.h"
#include "access/transam.h"
#include "catalog/catalog.h"
#include "catalog/namespace.h"
#include "catalog/pg_auth_members.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "catalog/pg_db_role_setting.h"
#include "catalog/pg_largeobject.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_parameter_acl.h"
#include "catalog/pg_replication_origin.h"
#include "catalog/pg_shdepend.h"
#include "catalog/pg_shdescription.h"
#include "catalog/pg_shseclabel.h"
#include "catalog/pg_subscription.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "utils/fmgroids.h"
#include "utils/fmgrprotos.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"

/* YB includes. */
#include "access/heapam.h"
#include "access/htup_details.h"
#include "catalog/pg_yb_tablegroup.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/pg_yb_logical_client_version.h"
#include "catalog/pg_yb_profile.h"
#include "catalog/pg_yb_role_profile.h"
#include "commands/defrem.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "pg_yb_utils.h"

/*
 * Parameters to determine when to emit a log message in
 * GetNewOidWithIndex()
 */
#define GETNEWOID_LOG_THRESHOLD 1000000
#define GETNEWOID_LOG_MAX_INTERVAL 128000000

/*
 * IsSystemRelation
 *		True iff the relation is either a system catalog or a toast table.
 *		See IsCatalogRelation for the exact definition of a system catalog.
 *
 *		We treat toast tables of user relations as "system relations" for
 *		protection purposes, e.g. you can't change their schemas without
 *		special permissions.  Therefore, most uses of this function are
 *		checking whether allow_system_table_mods restrictions apply.
 *		For other purposes, consider whether you shouldn't be using
 *		IsCatalogRelation instead.
 *
 *		This function does not perform any catalog accesses.
 *		Some callers rely on that!
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
	/* IsCatalogRelationOid is a bit faster, so test that first */
	return (IsCatalogRelationOid(relid) || IsToastClass(reltuple));
}

/*
 * IsCatalogRelation
 *		True iff the relation is a system catalog.
 *
 *		By a system catalog, we mean one that is created during the bootstrap
 *		phase of initdb.  That includes not just the catalogs per se, but
 *		also their indexes, and TOAST tables and indexes if any.
 *
 *		This function does not perform any catalog accesses.
 *		Some callers rely on that!
 */
bool
IsCatalogRelation(Relation relation)
{
	return IsCatalogRelationOid(RelationGetRelid(relation));
}

/*
 * IsCatalogRelationOid
 *		True iff the relation identified by this OID is a system catalog.
 *
 *		By a system catalog, we mean one that is created during the bootstrap
 *		phase of initdb.  That includes not just the catalogs per se, but
 *		also their indexes, and TOAST tables and indexes if any.
 *
 *		This function does not perform any catalog accesses.
 *		Some callers rely on that!
 */
bool
IsCatalogRelationOid(Oid relid)
{
	/*
	 * We consider a relation to be a system catalog if it has a pinned OID.
	 * This includes all the defined catalogs, their indexes, and their TOAST
	 * tables and indexes.
	 *
	 * This rule excludes the relations in information_schema, which are not
	 * integral to the system and can be treated the same as user relations.
	 * (Since it's valid to drop and recreate information_schema, any rule
	 * that did not act this way would be wrong.)
	 *
	 * This test is reliable since an OID wraparound will skip this range of
	 * OIDs; see GetNewObjectId().
	 */
	return (relid < (Oid) FirstUnpinnedObjectId);
}

/*
 * IsToastRelation
 *		True iff relation is a TOAST support relation (or index).
 *
 *		Does not perform any catalog accesses.
 */
bool
IsToastRelation(Relation relation)
{
	/*
	 * What we actually check is whether the relation belongs to a pg_toast
	 * namespace.  This should be equivalent because of restrictions that are
	 * enforced elsewhere against creating user relations in, or moving
	 * relations into/out of, a pg_toast namespace.  Notice also that this
	 * will not say "true" for toast tables belonging to other sessions' temp
	 * tables; we expect that other mechanisms will prevent access to those.
	 */
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
 * IsCatalogNamespace
 *		True iff namespace is pg_catalog.
 *
 *		Does not perform any catalog accesses.
 *
 * NOTE: the reason this isn't a macro is to avoid having to include
 * catalog/pg_namespace.h in a lot of places.
 */
bool
IsCatalogNamespace(Oid namespaceId)
{
	return namespaceId == PG_CATALOG_NAMESPACE;
}

/*
 * Same logic as with IsCatalogNamespace, to be used when OID isn't known.
 * NULL name results in InvalidOid.
 */
bool
YbIsCatalogNamespaceByName(const char *namespace_name)
{
	Oid namespace_oid;

	namespace_oid = namespace_name ?
		LookupExplicitNamespace(namespace_name, true) :
		InvalidOid;

	return IsCatalogNamespace(namespace_oid);
}

/*
 * IsToastNamespace
 *		True iff namespace is pg_toast or my temporary-toast-table namespace.
 *
 *		Does not perform any catalog accesses.
 *
 * Note: this will return false for temporary-toast-table namespaces belonging
 * to other backends.  Those are treated the same as other backends' regular
 * temp table namespaces, and access is prevented where appropriate.
 * If you need to check for those, you may be able to use isAnyTempNamespace,
 * but beware that that does involve a catalog access.
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
		relationId == DbRoleSettingRelationId ||
		relationId == ParameterAclRelationId ||
		relationId == ReplicationOriginRelationId ||
		relationId == SharedDependRelationId ||
		relationId == SharedDescriptionRelationId ||
		relationId == SharedSecLabelRelationId ||
		relationId == SubscriptionRelationId ||
		relationId == TableSpaceRelationId ||
		relationId == YBCatalogVersionRelationId ||
		relationId == YbProfileRelationId ||
		relationId == YbRoleProfileRelationId ||
		relationId == YBLogicalClientVersionRelationId)
		return true;
	/* These are their indexes */
	if (relationId == AuthIdOidIndexId ||
		relationId == AuthIdRolnameIndexId ||
		relationId == AuthMemMemRoleIndexId ||
		relationId == AuthMemRoleMemIndexId ||
		relationId == DatabaseNameIndexId ||
		relationId == DatabaseOidIndexId ||
		relationId == DbRoleSettingDatidRolidIndexId ||
		relationId == ParameterAclOidIndexId ||
		relationId == ParameterAclParnameIndexId ||
		relationId == ReplicationOriginIdentIndex ||
		relationId == ReplicationOriginNameIndex ||
		relationId == SharedDependDependerIndexId ||
		relationId == SharedDependReferenceIndexId ||
		relationId == SharedDescriptionObjIndexId ||
		relationId == SharedSecLabelObjectIndexId ||
		relationId == SubscriptionNameIndexId ||
		relationId == SubscriptionObjectIndexId ||
		relationId == TablespaceNameIndexId ||
		relationId == TablespaceOidIndexId ||
		relationId == YBCatalogVersionDbOidIndexId ||
		relationId == YbProfileOidIndexId ||
		relationId == YbProfileRolnameIndexId ||
		relationId == YbRoleProfileOidIndexId ||
		relationId == YBLogicalClientVersionDbOidIndexId)
		return true;
	/* These are their toast tables and toast indexes */
	if (relationId == PgAuthidToastTable ||
		relationId == PgAuthidToastIndex ||
		relationId == PgDatabaseToastTable ||
		relationId == PgDatabaseToastIndex ||
		relationId == PgDbRoleSettingToastTable ||
		relationId == PgDbRoleSettingToastIndex ||
		relationId == PgParameterAclToastTable ||
		relationId == PgParameterAclToastIndex ||
		relationId == PgReplicationOriginToastTable ||
		relationId == PgReplicationOriginToastIndex ||
		relationId == PgShdescriptionToastTable ||
		relationId == PgShdescriptionToastIndex ||
		relationId == PgShseclabelToastTable ||
		relationId == PgShseclabelToastIndex ||
		relationId == PgSubscriptionToastTable ||
		relationId == PgSubscriptionToastIndex ||
		relationId == PgTablespaceToastTable ||
		relationId == PgTablespaceToastIndex)
		return true;
	/* In test mode, there might be shared relations other than predefined ones. */
	if (yb_test_system_catalogs_creation)
	{
		/* To avoid cycle */
		if (relationId == RelationRelationId)
			return false;

		Relation  pg_class = table_open(RelationRelationId, AccessShareLock);
		HeapTuple tuple    = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relationId));

		bool result = HeapTupleIsValid(tuple)
			? ((Form_pg_class) GETSTRUCT(tuple))->relisshared
			: false;

		if (HeapTupleIsValid(tuple))
			heap_freetuple(tuple);

		table_close(pg_class, AccessShareLock);

		return result;
	}

	return false;
}

/*
 * IsPinnedObject
 *		Given the class + OID identity of a database object, report whether
 *		it is "pinned", that is not droppable because the system requires it.
 *
 * We used to represent this explicitly in pg_depend, but that proved to be
 * an undesirable amount of overhead, so now we rely on an OID range test.
 */
bool
IsPinnedObject(Oid classId, Oid objectId)
{
	/*
	 * Objects with OIDs above FirstUnpinnedObjectId are never pinned.  Since
	 * the OID generator skips this range when wrapping around, this check
	 * guarantees that user-defined objects are never considered pinned.
	 */
	if (objectId >= FirstUnpinnedObjectId)
		return false;

	/*
	 * Large objects are never pinned.  We need this special case because
	 * their OIDs can be user-assigned.
	 */
	if (classId == LargeObjectRelationId)
		return false;

	/*
	 * There are a few objects defined in the catalog .dat files that, as a
	 * matter of policy, we prefer not to treat as pinned.  We used to handle
	 * that by excluding them from pg_depend, but it's just as easy to
	 * hard-wire their OIDs here.  (If the user does indeed drop and recreate
	 * them, they'll have new but certainly-unpinned OIDs, so no problem.)
	 *
	 * Checking both classId and objectId is overkill, since OIDs below
	 * FirstGenbkiObjectId should be globally unique, but do it anyway for
	 * robustness.
	 */

	/* the public namespace is not pinned */
	if (classId == NamespaceRelationId &&
		objectId == PG_PUBLIC_NAMESPACE)
		return false;

	/*
	 * Databases are never pinned.  It might seem that it'd be prudent to pin
	 * at least template0; but we do this intentionally so that template0 and
	 * template1 can be rebuilt from each other, thus letting them serve as
	 * mutual backups (as long as you've not modified template1, anyway).
	 */
	if (classId == DatabaseRelationId)
		return false;

	/*
	 * YB: Vector type is not pinned so that it can be dropped by the vector
	 * extension.
	 */
	if (objectId == VECTOROID)
		return false;

	/*
	 * All other initdb-created objects are pinned.  This is overkill (the
	 * system doesn't really depend on having every last weird datatype, for
	 * instance) but generating only the minimum required set of dependencies
	 * seems hard, and enforcing an accurate list would be much more expensive
	 * than the simple range test used here.
	 */
	return true;
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

	/* see notes above about using SnapshotAny */
	scan = systable_beginscan(relation, indexId, true, SnapshotAny, 1, &key);

	collides = HeapTupleIsValid(systable_getnext(scan));

	systable_endscan(scan);

	return collides;
}

/*
 * GetNewOidWithIndex
 *		Generate a new OID that is unique within the system relation.
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
 *
 * Note that we are effectively assuming that the table has a relatively small
 * number of entries (much less than 2^32) and there aren't very long runs of
 * consecutive existing OIDs.  This is a mostly reasonable assumption for
 * system catalogs.
 *
 * Caller must have a suitable lock on the relation.
 */
Oid
GetNewOidWithIndex(Relation relation, Oid indexId, AttrNumber oidcolumn)
{
	Oid			newOid;
	uint64		retries = 0;
	uint64		retries_before_log = GETNEWOID_LOG_THRESHOLD;

	/* Only system relations are supported */
	Assert(IsSystemRelation(relation));

	/* In bootstrap mode, we don't have any indexes to use */
	if (IsBootstrapProcessingMode())
		return GetNewObjectId();

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
			YbDatabaseIdForNewObjectId = Template1DbOid;
		else
			YbDatabaseIdForNewObjectId = MyDatabaseId;
	}

	/* Generate new OIDs until we find one not in the table */
	do
	{
		CHECK_FOR_INTERRUPTS();

		newOid = GetNewObjectId();

		/*
		 * Log that we iterate more than GETNEWOID_LOG_THRESHOLD but have not
		 * yet found OID unused in the relation. Then repeat logging with
		 * exponentially increasing intervals until we iterate more than
		 * GETNEWOID_LOG_MAX_INTERVAL. Finally repeat logging every
		 * GETNEWOID_LOG_MAX_INTERVAL unless an unused OID is found. This
		 * logic is necessary not to fill up the server log with the similar
		 * messages.
		 */
		if (retries >= retries_before_log)
		{
			ereport(LOG,
					(errmsg("still searching for an unused OID in relation \"%s\"",
							RelationGetRelationName(relation)),
					 errdetail_plural("OID candidates have been checked %llu time, but no unused OID has been found yet.",
									  "OID candidates have been checked %llu times, but no unused OID has been found yet.",
									  retries,
									  (unsigned long long) retries)));

			/*
			 * Double the number of retries to do before logging next until it
			 * reaches GETNEWOID_LOG_MAX_INTERVAL.
			 */
			if (retries_before_log * 2 <= GETNEWOID_LOG_MAX_INTERVAL)
				retries_before_log *= 2;
			else
				retries_before_log += GETNEWOID_LOG_MAX_INTERVAL;
		}

		retries++;
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
 * As with GetNewOidWithIndex(), there is some theoretical risk of a race
 * condition, but it doesn't seem worth worrying about.
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
			rnode.node.relNode = GetNewOidWithIndex(pg_class, ClassOidIndexId,
													Anum_pg_class_oid);
		else
			rnode.node.relNode = GetNewObjectId();

		/* Check for existing file of same name */
	} while (DoesRelFileExist(&rnode));

	return rnode.node.relNode;
}

/*
 * SQL callable interface for GetNewOidWithIndex().  Outside of initdb's
 * direct insertions into catalog tables, and recovering from corruption, this
 * should rarely be needed.
 *
 * Function is intentionally not documented in the user facing docs.
 */
Datum
pg_nextoid(PG_FUNCTION_ARGS)
{
	Oid			reloid = PG_GETARG_OID(0);
	Name		attname = PG_GETARG_NAME(1);
	Oid			idxoid = PG_GETARG_OID(2);
	Relation	rel;
	Relation	idx;
	HeapTuple	atttuple;
	Form_pg_attribute attform;
	AttrNumber	attno;
	Oid			newoid;

	/*
	 * As this function is not intended to be used during normal running, and
	 * only supports system catalogs (which require superuser permissions to
	 * modify), just checking for superuser ought to not obstruct valid
	 * usecases.
	 */
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to call %s()",
						"pg_nextoid")));

	rel = table_open(reloid, RowExclusiveLock);
	idx = index_open(idxoid, RowExclusiveLock);

	if (!IsSystemRelation(rel))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("pg_nextoid() can only be used on system catalogs")));

	if (idx->rd_index->indrelid != RelationGetRelid(rel))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("index \"%s\" does not belong to table \"%s\"",
						RelationGetRelationName(idx),
						RelationGetRelationName(rel))));

	atttuple = SearchSysCacheAttName(reloid, NameStr(*attname));
	if (!HeapTupleIsValid(atttuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						NameStr(*attname), RelationGetRelationName(rel))));

	attform = ((Form_pg_attribute) GETSTRUCT(atttuple));
	attno = attform->attnum;

	if (attform->atttypid != OIDOID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("column \"%s\" is not of type oid",
						NameStr(*attname))));

	if (IndexRelationGetNumberOfKeyAttributes(idx) != 1 ||
		idx->rd_index->indkey.values[0] != attno)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("index \"%s\" is not the index for column \"%s\"",
						RelationGetRelationName(idx),
						NameStr(*attname))));

	newoid = GetNewOidWithIndex(rel, idxoid, attno);

	ReleaseSysCache(atttuple);
	table_close(rel, RowExclusiveLock);
	index_close(idx, RowExclusiveLock);

	PG_RETURN_OID(newoid);
}

/*
 * SQL callable interface for StopGeneratingPinnedObjectIds().
 *
 * This is only to be used by initdb, so it's intentionally not documented in
 * the user facing docs.
 */
Datum
pg_stop_making_pinned_objects(PG_FUNCTION_ARGS)
{
	/*
	 * Belt-and-suspenders check, since StopGeneratingPinnedObjectIds will
	 * fail anyway in non-single-user mode.
	 */
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("must be superuser to call %s()",
						"pg_stop_making_pinned_objects")));

	StopGeneratingPinnedObjectIds();

	PG_RETURN_VOID();
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

	/* TODO(Alex): The relcache will cache the identity of the OID index for us */
	oidIndex = ClassOidIndexId;

	if (!OidIsValid(oidIndex))
	{
		elog(WARNING, "Could not find oid index of pg_class.");
	}

	collides = DoesOidExistInRelation(table_oid,
									  pg_class,
									  oidIndex,
									  Anum_pg_class_oid);

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
					table_open(RelationRelationId, RowExclusiveLock);
				is_oid_free = IsTableOidUnused(table_oid,
											   reltablespace,
											   pg_class_desc,
											   relpersistence);
				table_close(pg_class_desc, RowExclusiveLock);

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
				pg_type_desc = table_open(TypeRelationId, AccessExclusiveLock);

				tuple = SearchSysCacheCopy1(TYPEOID, ObjectIdGetDatum(row_type_oid));
				if (HeapTupleIsValid(tuple))
					ereport(ERROR,
							(errcode(ERRCODE_DUPLICATE_OBJECT),
							 errmsg("type OID %u is in use", row_type_oid)));

				table_close(pg_type_desc, AccessExclusiveLock);

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
	if (IsCatalogNamespace(namespaceId) ||
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
