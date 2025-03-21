/*-------------------------------------------------------------------------
 *
 * pg_yb_invalidation_messages.h
 *
 *	  definition of the "YSQL invalidation messages" system catalog (pg_yb_invalidation_messages)
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * src/include/catalog/pg_yb_invalidation_messages.h
 *
 * NOTES
 *	  The Catalog.pm module reads this file and derives schema
 *	  information.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_YB_INVALIDATION_MESSAGES_H
#define PG_YB_INVALIDATION_MESSAGES_H

#include "catalog/genbki.h"
#include "catalog/pg_yb_invalidation_messages_d.h"

/* ----------------
 *		pg_yb_invalidation_messages definition.  cpp turns this into
 *		typedef struct FormData_yb_pg_invalidation_messages
 *      The YbInvalidationMessagesRelationId value (8080) is also used in docdb
 *      (kPgYbCatalogVersionTableOid in entity_ids.cc) so this value here
 *      should match kPgYbCatalogVersionTableOid.
 * ----------------
 */
CATALOG(pg_yb_invalidation_messages,8080,YbInvalidationMessagesRelationId) BKI_SHARED_RELATION BKI_ROWTYPE_OID(8081,YbInvalidationMessagesRelation_Rowtype_Id) BKI_SCHEMA_MACRO
{
	/* Oid of the database this applies to. */
	Oid			db_oid;

	/* Current version of the catalog. */
	int64		current_version;

	/* Timestamp (in seconds since unix epoch) when this row was added. */
	int64		message_time;

	/* Invalidate messages. */
	bytea		messages;
} FormData_yb_pg_invalidation_messages;

/* ----------------
 *		Form_yb_pg_invalidation_messages corresponds to a pointer to a tuple with
 *		the format of pg_yb_invalidation_messages relation.
 * ----------------
 */
typedef FormData_yb_pg_invalidation_messages *Form_yb_pg_invalidation_messages;

DECLARE_UNIQUE_INDEX_PKEY(pg_yb_invalidation_messages_db_oid_current_version_index, 8082, YbInvalidationMessagesIndexId, on pg_yb_invalidation_messages using btree(db_oid oid_ops, current_version int8_ops));

#endif							/* PG_YB_INVALIDATION_MESSAGES_H */
