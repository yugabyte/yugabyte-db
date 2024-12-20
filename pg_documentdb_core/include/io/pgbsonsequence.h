/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/pgbsonsequence.h
 *
 * The BSON Sequence type serialization.
 *
 *-------------------------------------------------------------------------
 */

#ifndef PG_BSONSEQUENCE_H
#define PG_BSONSEQUENCE_H

#include <nodes/pg_list.h>

typedef struct
{
	int32 vl_len_;              /* varlena header (do not touch directly!) */
	char vl_dat[FLEXIBLE_ARRAY_MEMBER];         /* bsonsequence is here */
} pgbsonsequence;

#define DatumGetPgBsonSequence(n) ((pgbsonsequence *) PG_DETOAST_DATUM(n))
#define PG_GETARG_PGBSON_SEQUENCE(n) (DatumGetPgBsonSequence(PG_GETARG_DATUM(n)))
#define PG_GETARG_MAYBE_NULL_PGBSON_SEQUENCE(n) PG_ARGISNULL(n) ? NULL : \
	((pgbsonsequence *) PG_DETOAST_DATUM(PG_GETARG_DATUM(n)))

List * PgbsonSequenceGetDocumentBsonValues(const pgbsonsequence *bsonSequence);

#endif
