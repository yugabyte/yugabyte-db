/*-------------------------------------------------------------------------
 *
 * pg_yb_tablegroup.h
 *	  definition of the "tablegroup" system catalog (pg_yb_tablegroup)
 *
 *
 * Copyright (c) YugaByte, Inc.
 *
 * src/include/catalog/pg_yb_tablegroup.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_TABLEGROUP_H
#define PG_YB_TABLEGROUP_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_tablegroup_d.h"

/* ----------------
 *		pg_yb_tablegroup definition.  cpp turns this into
 *		typedef struct FormData_pg_yb_tablegroup
 * ----------------
 */
CATALOG(pg_yb_tablegroup,8036,YbTablegroupRelationId) BKI_ROWTYPE_OID(8038,YbTablegroupRelation_Rowtype_Id) BKI_SCHEMA_MACRO
{
	Oid			oid;			/* oid */
	NameData	grpname;		/* tablegroup name */
	Oid			grpowner;		/* owner of tablegroup */
	Oid			grptablespace;  /* tablespace of tablegroup */

#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	aclitem		grpacl[1];		/* access permissions */
	text		grpoptions[1];	/* per-tablegroup options */
#endif
} FormData_pg_yb_tablegroup;

/* ----------------
 *		Form_pg_yb_tablegroup corresponds to a pointer to a tuple with
 *		the format of pg_yb_tablegroup relation.
 * ----------------
 */
typedef FormData_pg_yb_tablegroup *Form_pg_yb_tablegroup;

DECLARE_UNIQUE_INDEX_PKEY(pg_yb_tablegroup_oid_index, 8037, YbTablegroupOidIndexId, on pg_yb_tablegroup using btree(oid oid_ops));

#endif							/* PG_YB_TABLEGROUP_H */
