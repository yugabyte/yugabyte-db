/*-------------------------------------------------------------------------
 *
 * yb_catalog_version.h
 *	  utility functions related to the ysql catalog version table.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * src/include/catalog/yb_catalog_version.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "yb/yql/pggate/ybc_pg_typedefs.h"

/*
 * Enum representing how the catalog version is stored on this cluster.
 * Needed for backwards-compatibility with old clusters.
 * Should only be set once per process (first time the catalog version is
 * requested) and never modified afterwards.
 * TODO: Once cluster/initdb upgrade is supported (#2272) we should use it
 * to upgrade old cluster and remove the now-obsolete protobuf-based paths.
 */
typedef enum YbCatalogVersionType
{
	CATALOG_VERSION_UNSET,           /* Not yet set. */
	CATALOG_VERSION_PROTOBUF_ENTRY,  /* Old protobuf-based version. */
	CATALOG_VERSION_CATALOG_TABLE,   /* New table-based version. */
} YbCatalogVersionType;

extern YbCatalogVersionType yb_catalog_version_type;

/* Get the latest catalog version from the master leader. */
extern uint64_t YbGetMasterCatalogVersion();

/* Send a request to increment the master catalog version. */
extern bool YbIncrementMasterCatalogVersionTableEntry(bool is_breaking_change,
													  bool is_global_ddl,
													  const char *command_tag);

/* Send a request to create the master catalog version for the given database. */
extern void YbCreateMasterDBCatalogVersionTableEntry(Oid db_oid);

/* Send a request to delete the master catalog version for the given database. */
extern void YbDeleteMasterDBCatalogVersionTableEntry(Oid db_oid);

/* Annotate an DML request if it changes the catalog data (if needed). */
extern bool YbMarkStatementIfCatalogVersionIncrement(
	YBCPgStatement ybc_stmt,
	Relation rel);

extern bool YbIsSystemCatalogChange(Relation rel);

/* Return the catalog version type. */
extern YbCatalogVersionType YbGetCatalogVersionType();

/* Get actual db_oid for pg_yb_catalog_version */
Oid YbMasterCatalogVersionTableDBOid();
