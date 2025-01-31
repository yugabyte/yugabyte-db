/*-------------------------------------------------------------------------
 *
 * pg_yb_role_profile.h
 *	  definition of the "role_profile" system catalog (pg_yb_role_profile)
 *
 *
 * Copyright (c) Yugabyte, Inc.
 *
 * src/include/catalog/pg_yb_role_profile.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_ROLE_PROFILE_H
#define PG_YB_ROLE_PROFILE_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_role_profile_d.h"

/* ----------------
 *		pg_yb_role_profile definition.  cpp turns this into
 *		typedef struct FormData_pg_yb_role_profile
 * ----------------
 */
CATALOG(pg_yb_role_profile,8054,YbRoleProfileRelationId) BKI_SHARED_RELATION BKI_ROWTYPE_OID(8056,YbRoleProfileRelation_Rowtype_Id) BKI_SCHEMA_MACRO
{
	Oid			oid;			/* oid */
	Oid			rolprfrole;		/* OID of the role */
	Oid			rolprfprofile;	/* OID of the profile */
	char		rolprfstatus;	/* Refer to the status categories below */
	int32		rolprffailedloginattempts;	/* Number of failed attempts */
#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	/* Lock timeout expiration time, if any */
	timestamptz rolprflockeduntil BKI_FORCE_NULL BKI_DEFAULT(_null_);

#endif
} FormData_pg_yb_role_profile;

/* ----------------
 *		Form_pg_yb_role_profile corresponds to a pointer to a tuple with
 *		the format of pg_yb_role_profile relation.
 * ----------------
 */
typedef FormData_pg_yb_role_profile *Form_pg_yb_role_profile;

DECLARE_UNIQUE_INDEX_PKEY(pg_yb_role_profile_oid_index, 8055, YbRoleProfileOidIndexId, on pg_yb_role_profile using btree(oid oid_ops));

/*
 * Symbolic values for rolprfstatus
 */
#define YB_ROLPRFSTATUS_OPEN 'o'	/* OPEN. Role is unlocked and can login. */
#define YB_ROLPRFSTATUS_LOCKED 'l'	/* LOCKED. Role is locked and cannot
									 * login. */

#endif							/* PG_YB_ROLE_PROFILE_H */
