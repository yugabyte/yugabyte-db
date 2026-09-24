/*-------------------------------------------------------------------------
 *
 * pg_yb_password_history.h
 *	  definition of the "password history" system catalog (pg_yb_password_history)
 *
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * src/include/catalog/pg_yb_password_history.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_PASSWORD_HISTORY_H
#define PG_YB_PASSWORD_HISTORY_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_password_history_d.h"

/* ----------------
 *		pg_yb_password_history definition.  cpp turns this into
 *		typedef struct FormData_pg_yb_password_history
 * ----------------
 */
CATALOG(pg_yb_password_history,8118,YbPasswordHistoryRelationId) BKI_SHARED_RELATION BKI_ROWTYPE_OID(8119,YbPasswordHistoryRelation_Rowtype_Id) BKI_SCHEMA_MACRO
{
	Oid			pwdhstrole BKI_LOOKUP(pg_authid);	/* OID of the role (references pg_authid.oid) */
#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	timestamptz pwdhstchangetime BKI_FORCE_NOT_NULL;	/* time this password was set */
	/* Secret as stored in pg_authid.rolpassword */
	text		pwdhstpassword BKI_FORCE_NOT_NULL;
#endif
} FormData_pg_yb_password_history;

/* ----------------
 *		Form_pg_yb_password_history corresponds to a pointer to a tuple with
 *		the format of pg_yb_password_history relation.
 * ----------------
 */
typedef FormData_pg_yb_password_history *Form_pg_yb_password_history;

DECLARE_UNIQUE_INDEX_PKEY(pg_yb_password_history_pwdhstrole_pwdhstchangetime_index, 8120, YbPasswordHistoryPwdhstrolePwdhstchangetimeIndexId, on pg_yb_password_history using btree(pwdhstrole oid_ops, pwdhstchangetime timestamptz_ops));

#endif							/* PG_YB_PASSWORD_HISTORY_H */
