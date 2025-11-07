/*-------------------------------------------------------------------------
 *
 * yb_logical_client_version.h
 *	  utility functions related to the ysql catalog version table.
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 * src/include/catalog/yb_logical_client_version.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

/*
 * Enum representing how the catalog version is stored on this cluster.
 * Needed for backwards-compatibility with old clusters.
 * Should only be set once per process (first time the catalog version is
 * requested) and never modified afterwards.
 * TODO: Once cluster/initdb upgrade is supported (#2272) we should use it
 * to upgrade old cluster and remove the now-obsolete protobuf-based paths.
 */
typedef enum YbLogicalClientVersionType
{
	LOGICAL_CLIENT_VERSION_UNSET,	/* Not yet set. */
	LOGICAL_CLIENT_VERSION_CATALOG_TABLE,	/* New table-based version. */
} YbLogicalClientVersionType;

extern YbLogicalClientVersionType yb_logical_client_version_type;

/* Send a request to increment the master logical client version. */
extern bool YbIncrementMasterLogicalClientVersionTableEntry();

/* Send a request to create the master logical client version for the given database. */
extern void YbCreateMasterDBLogicalClientVersionTableEntry(Oid db_oid);

/* Send a request to delete the master logical client version for the given database. */
extern void YbDeleteMasterDBLogicalClientVersionTableEntry(Oid db_oid);

extern YbLogicalClientVersionType YbGetLogicalClientVersionType();

/* Get the latest logical client version from the master leader. */
extern uint64_t YbGetMasterLogicalClientVersion();
