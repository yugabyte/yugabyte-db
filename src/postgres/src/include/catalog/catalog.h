/*-------------------------------------------------------------------------
 *
 * catalog.h
 *	  prototypes for functions in backend/catalog/catalog.c
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/catalog.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef CATALOG_H
#define CATALOG_H

#include "catalog/pg_class.h"
#include "utils/relcache.h"

/*
 * This OID corresponds to the last used OID in the block of OIDs that are used
 * by YB specific catalog additions, starting at 8000. When making changes to
 * the catalog by adding a new OID in 'pg_*.dat', 'pg_*.h', 'toasting.h', or
 * 'indexing.h', make sure to increment this value. Additionally, the script
 * 'catalog/unused_oids' will help by outputting the blocks of unused OIDs to
 * validate that this value is up to date.
 * TODO(#14536): add a test to validate the block of unused OIDs.
 *
 * If you increment it, make sure you didn't forget to add a new SQL migration
 * (see pg_yb_migration.dat and src/yb/yql/pgwrapper/ysql_migrations/README.md)
 */
#define YB_LAST_USED_OID 8064

extern bool IsSystemRelation(Relation relation);
extern bool IsToastRelation(Relation relation);
extern bool IsCatalogRelation(Relation relation);

extern bool IsSystemClass(Oid relid, Form_pg_class reltuple);
extern bool IsToastClass(Form_pg_class reltuple);

extern bool IsCatalogRelationOid(Oid relid);

extern bool IsCatalogNamespace(Oid namespaceId);
extern bool IsToastNamespace(Oid namespaceId);

extern bool IsReservedName(const char *name);

extern bool IsSharedRelation(Oid relationId);

extern bool IsPinnedObject(Oid classId, Oid objectId);

extern Oid	GetNewOidWithIndex(Relation relation, Oid indexId,
							   AttrNumber oidcolumn);
extern Oid	GetNewRelFileNode(Oid reltablespace, Relation pg_class,
							  char relpersistence);

// TODO: Rename according to new style guide
extern bool YbIsCatalogNamespaceByName(const char *namespace_name);

extern Oid GetTableOidFromRelOptions(List *relOptions, Oid reltablespace,
									 char relpersistence);

extern Oid GetRowTypeOidFromRelOptions(List *relOptions);

extern Oid YbGetColocationIdFromRelOptions(List *relOptions);

extern bool YbGetUseInitdbAclFromRelOptions(List *options);

#endif							/* CATALOG_H */
