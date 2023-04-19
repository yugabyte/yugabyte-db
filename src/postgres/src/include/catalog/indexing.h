/*-------------------------------------------------------------------------
 *
 * indexing.h
 *	  This file provides some definitions to support indexing
 *	  on system catalogs
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/indexing.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEXING_H
#define INDEXING_H

#include "access/htup.h"
#include "nodes/execnodes.h"
#include "utils/relcache.h"

/*
 * The state object used by CatalogOpenIndexes and friends is actually the
 * same as the executor's ResultRelInfo, but we give it another type name
 * to decouple callers from that fact.
 */
typedef struct ResultRelInfo *CatalogIndexState;

/*
 * Cap the maximum amount of bytes allocated for multi-inserts with system
 * catalogs, limiting the number of slots used.
 */
#define MAX_CATALOG_MULTI_INSERT_BYTES 65535

/*
 * indexing.c prototypes
 */
extern CatalogIndexState CatalogOpenIndexes(Relation heapRel);
extern void CatalogCloseIndexes(CatalogIndexState indstate);
extern void CatalogTupleInsert(Relation heapRel, HeapTuple tup);
extern void CatalogTupleInsertWithInfo(Relation heapRel, HeapTuple tup,
									   CatalogIndexState indstate, bool yb_shared_insert);
extern void CatalogTuplesMultiInsertWithInfo(Relation heapRel,
											 TupleTableSlot **slot,
											 int ntuples,
											 CatalogIndexState indstate,
											 bool yb_shared_insert);
extern void CatalogTupleUpdate(Relation heapRel, ItemPointer otid,
							   HeapTuple tup);
extern void CatalogTupleUpdateWithInfo(Relation heapRel,
									   ItemPointer otid, HeapTuple tup,
									   CatalogIndexState indstate);
extern void CatalogTupleDelete(Relation heapRel, HeapTuple tup);

extern void YBCatalogTupleInsert(Relation heapRel, HeapTuple tup, bool yb_shared_insert);

/* YB_TODO(neil)
 * Postgres 15 either changes or moves these code elsewhere
 */
/*
 * These macros are just to keep the C compiler from spitting up on the
 * upcoming commands for Catalog.pm.
 */
#define DECLARE_INDEX(name,oid,decl) extern int no_such_variable
#define DECLARE_UNIQUE_INDEX(name,oid,decl) extern int no_such_variable

DECLARE_UNIQUE_INDEX(pg_yb_catalog_version_db_oid_index, 8012, on pg_yb_catalog_version using btree(db_oid oid_ops));
#define YBCatalogVersionDbOidIndexId 8012

DECLARE_UNIQUE_INDEX(pg_yb_profile_oid_index, 8052, on pg_yb_profile using btree(oid oid_ops));
#define YbProfileOidIndexId 8052

DECLARE_UNIQUE_INDEX(pg_yb_role_profile_oid_index, 8055, on pg_yb_role_profile using btree(oid oid_ops));
#define YbRoleProfileOidIndexId 8055

DECLARE_UNIQUE_INDEX(pg_yb_profile_prfname_index, 8057, on pg_yb_profile using btree(prfname name_ops));
#define YbProfileRolnameIndexId	8057

#endif							/* INDEXING_H */
