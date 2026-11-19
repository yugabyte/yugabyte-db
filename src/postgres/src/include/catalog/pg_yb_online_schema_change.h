/*-------------------------------------------------------------------------
 *
 * pg_yb_online_schema_change.h
 *	  definition of the "online schema change jobs" system catalog
 *	  (pg_yb_online_schema_change)
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 * src/include/catalog/pg_yb_online_schema_change.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *	  Each row is one online schema change job.  status is one of:
 *	    "active"   - from yb_begin_mirror_table until the job ends.
 *	    "finished" - yb_finish_mirror_table swapped the tables.
 *	    "aborted"  - yb_abort_mirror_table stopped the job.
 *
 *	  Postgres records only these steps.  The copy belongs to the master,
 *	  which keeps its state durably: whether an active job's copy is running,
 *	  done or failed is always read from the master, and the
 *	  yb_online_schema_change view shows it.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_ONLINE_SCHEMA_CHANGE_H
#define PG_YB_ONLINE_SCHEMA_CHANGE_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_online_schema_change_d.h"

/* ----------------
 *		pg_yb_online_schema_change definition. cpp turns this into
 *		typedef struct FormData_pg_yb_online_schema_change
 * ----------------
 */
CATALOG(pg_yb_online_schema_change,8124,YbOnlineSchemaChangeRelationId) BKI_ROWTYPE_OID(8125,YbOnlineSchemaChangeRelation_Rowtype_Id) BKI_SCHEMA_MACRO
{
	int64		job_id;			/* online schema change job id */
	Oid			source_table_oid;	/* table being changed (source) */
	Oid			target_table_oid;	/* table the source is copied into */
	Oid			transform_fn_oid;	/* row transform function */

#ifdef CATALOG_VARLEN			/* variable-length fields start here */
	timestamptz	created_at;		/* when the job began */
	timestamptz	updated_at;		/* last status change */
	text		status BKI_FORCE_NOT_NULL;	/* see NOTES above */
#endif
} FormData_pg_yb_online_schema_change;

/* ----------------
 *		Form_pg_yb_online_schema_change corresponds to a pointer to a tuple
 *		with the format of pg_yb_online_schema_change relation.
 * ----------------
 */
typedef FormData_pg_yb_online_schema_change *Form_pg_yb_online_schema_change;

DECLARE_UNIQUE_INDEX_PKEY(pg_yb_online_schema_change_job_id_index, 8129, YbOnlineSchemaChangeJobIdIndexId, on pg_yb_online_schema_change using btree(job_id int8_ops));

#endif							/* PG_YB_ONLINE_SCHEMA_CHANGE_H */
