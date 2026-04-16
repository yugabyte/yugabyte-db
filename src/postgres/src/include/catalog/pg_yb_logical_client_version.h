/*-------------------------------------------------------------------------
 *
 * pg_yb_logical_client_version.h
 *
 *    definition of the "YSQL logical client version" system catalog (pg_yb_logical_client_version)
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 * src/include/catalog/pg_yb_logical_client_version.h
 *
 * NOTES
 *    The Catalog.pm module reads this file and derives schema
 *    information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_LOGICAL_CLIENT_VERSION_H
#define PG_YB_LOGICAL_CLIENT_VERSION_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_logical_client_version_d.h"

/* ----------------
 *    pg_yb_logical_client_version definition. cpp turns this into
 *    typedef struct FormData_yb_pg_logical_client_version
 * ----------------
 */
CATALOG(pg_yb_logical_client_version,8073,YBLogicalClientVersionRelationId) BKI_SHARED_RELATION BKI_ROWTYPE_OID(8074,YBLogicalClientVersionRelation_Rowtype_Id) BKI_SCHEMA_MACRO
{
	/* Oid of the database this applies to. */
	Oid			db_oid;

	/* Current version of the logical client. */
	int64		current_version;
} FormData_yb_pg_logical_client_version;

/* ----------------
 *    Form_yb_pg_logical_client_version corresponds to a pointer to a tuple with
 *    the format of pg_yb_logical_client_version relation.
 * ----------------
 */
typedef FormData_yb_pg_logical_client_version *Form_yb_pg_logical_client_version;

DECLARE_UNIQUE_INDEX_PKEY(pg_yb_logical_client_version_db_oid_index, 8075, YBLogicalClientVersionDbOidIndexId, on pg_yb_logical_client_version using btree(db_oid oid_ops));

#endif							/* PG_YB_LOGICAL_CLIENT_VERSION_H */
