/*-------------------------------------------------------------------------
 *
 * pg_yb_profile.h
 *	  definition of the "profile" system catalog (pg_yb_profile)
 *
 *
 * Copyright (c) Yugabyte, Inc.
 *
 * src/include/catalog/pg_yb_profile.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_PROFILE_H
#define PG_YB_PROFILE_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_profile_d.h"

/* ----------------
 *		pg_yb_profile definition.  cpp turns this into
 *		typedef struct FormData_pg_yb_profile
 * ----------------
 */
CATALOG(pg_yb_profile,8051,YbProfileRelationId) BKI_SHARED_RELATION BKI_ROWTYPE_OID(8053,YbProfileRelation_Rowtype_Id) BKI_SCHEMA_MACRO
{
	NameData	prfname;					/* profile name */
	int32		prfmaxfailedloginattempts;	/* no. of attempts allowed */
	int32		prfpasswordlocktime;		/* secs to lock out an account */
} FormData_pg_yb_profile;

/* ----------------
 *		Form_pg_yb_profile corresponds to a pointer to a tuple with
 *		the format of pg_yb_profile relation.
 * ----------------
 */
typedef FormData_pg_yb_profile *Form_pg_yb_profile;

#ifdef YB_TODO
/* YB_TODO(neil) Need to rework on these entries.
 * Postgres doesn't use these macros any longer?
 */
DECLARE_UNIQUE_INDEX(pg_yb_catalog_version_db_oid_index, 8012, on pg_yb_catalog_version using btree(db_oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_yb_profile_oid_index, 8052, on pg_yb_profile using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_yb_role_profile_oid_index, 8055, on pg_yb_role_profile using btree(oid oid_ops));
DECLARE_UNIQUE_INDEX(pg_yb_profile_prfname_index, 8057, on pg_yb_profile using btree(prfname name_ops));
#endif

#define YBCatalogVersionDbOidIndexId 8012

#define YbProfileOidIndexId 8052

#define YbRoleProfileOidIndexId 8055

#define YbProfileRolnameIndexId	8057

#endif							/* PG_YB_PROFILE_H */
