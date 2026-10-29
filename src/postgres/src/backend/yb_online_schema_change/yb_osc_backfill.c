/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_backfill.c
 *    Copy part of one source tablet into an online schema change target.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_backfill.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/table.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/lsyscache.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb_osc_internal.h"


/*
 * The INSERT's target column list: the live columns of dstTupdesc, matching
 * the SELECT list that YbOscTransformDeparse produces.
 */
static char *
BuildDstColumnList(TupleDesc dstTupdesc)
{
	StringInfoData buf;
	int			ncols = 0;

	initStringInfo(&buf);

	for (int i = 0; i < dstTupdesc->natts; ++i)
	{
		Form_pg_attribute attr = TupleDescAttr(dstTupdesc, i);

		if (attr->attisdropped)
			continue;
		if (ncols > 0)
			appendStringInfoString(&buf, ", ");
		appendStringInfoString(&buf, quote_identifier(NameStr(attr->attname)));
		ncols++;
	}
	return buf.data;
}

/*
 * Hex-encode src into a C string, the form in which exec_params carries
 * partition_key and bfinstr.  NULL stays NULL.
 */
static char *
HexEncodeBytea(bytea *src)
{
	if (src == NULL)
		return NULL;

	size_t		len = VARSIZE_ANY_EXHDR(src);
	char	   *hex = palloc(len * 2 + 1);

	hex_encode(VARDATA_ANY(src), len, hex);
	hex[len * 2] = '\0';
	return hex;
}

/*
 * Build the (rows_processed, next_bfinstr) result from the hex backfill spec
 * the scan returned.  next_bfinstr is NULL if the scan returned none.
 */
static Datum
BuildChunkResult(FunctionCallInfo fcinfo, int64 rowsProcessed,
				 YbPgExecOutParam *outParam)
{
	TupleDesc	tupdesc;

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
		elog(ERROR, "return type must be a row type");
	BlessTupleDesc(tupdesc);

	Datum		values[2];
	bool		nulls[2] = {false, false};

	values[0] = Int64GetDatum(rowsProcessed);

	if (outParam->bfoutput->len == 0)
	{
		nulls[1] = true;
		values[1] = (Datum) 0;
	}
	else
	{
		size_t		hexLen = outParam->bfoutput->len;
		size_t		decodedLen = hexLen / 2;
		bytea	   *nextBfinstr = (bytea *) palloc(VARHDRSZ + decodedLen);

		SET_VARSIZE(nextBfinstr, VARHDRSZ + decodedLen);
		hex_decode(outParam->bfoutput->data, hexLen, VARDATA(nextBfinstr));
		values[1] = PointerGetDatum(nextBfinstr);
	}

	return HeapTupleGetDatum(heap_form_tuple(tupdesc, values, nulls));
}

/*
 * yb_backfill_online_schema_change_chunk(src_oid, dst_oid, transform_oid,
 *                                read_time_ht, partition_key, bfinstr)
 *     RETURNS (rows_processed int8, next_bfinstr bytea)
 *
 * Called by the tserver, not by user SQL.  Copies rows of the source tablet
 * that starts at partition_key into the target, starting at the resume
 * key in bfinstr (a serialized PgsqlBackfillSpecPB) and stopping after its row
 * limit.  Returns the number of rows copied and the backfill spec the scan
 * returned (NULL if none), whose next_row_key is where the next call resumes.
 *
 * The copy is one INSERT ... SELECT that reads and writes at read_time_ht.
 * The base-table write is blind and non-transactional: DocDB skips the
 * duplicate check for a non-index backfill insert, so a row the
 * change-capture trigger already wrote at a later time wins by MVCC instead
 * of raising.  Unique secondary indexes are still checked.
 *
 * The statement is planned before yb_backfill_timestamp is set and executed
 * after, because planning must read the catalog at the current time.  This
 * is why it uses SPI_prepare rather than PL/pgSQL, which plans lazily during
 * execution and fails with "record type has not been registered".
 *
 * The transform is compiled, planned and run as the source table's owner, as
 * an index backfill runs index expressions as the table's owner, so that
 * whatever the transform calls does not run as the superuser the tserver
 * connects as.  The INSERT is therefore checked against the owner's
 * privileges on the target.
 */
PG_FUNCTION_INFO_V1(yb_backfill_online_schema_change_chunk);

Datum
yb_backfill_online_schema_change_chunk(PG_FUNCTION_ARGS)
{
	Oid			srcOid = PG_GETARG_OID(0);
	Oid			dstOid = PG_GETARG_OID(1);
	Oid			transformOid = PG_GETARG_OID(2);
	int64		readTimeHt = PG_GETARG_INT64(3);
	bytea	   *partitionKeyBytea = PG_ARGISNULL(4) ? NULL : PG_GETARG_BYTEA_PP(4);
	bytea	   *bfinstrBytea = PG_ARGISNULL(5) ? NULL : PG_GETARG_BYTEA_PP(5);

	/*
	 * The caller picks the tables, the transform and the write time, and the
	 * transform then runs as the source's owner, so only the tserver's copy
	 * connection may call this.  That connection kind requires yb-tserver-key
	 * authentication.  yb_is_tserver_auth_method alone would not do, since
	 * connection manager backends, which run client SQL, also have it.
	 */
	if (MyBackendType != YB_OSC_BACKFILL_BACKEND)
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("yb_backfill_online_schema_change_chunk can only be called by the tserver")));

	if (bfinstrBytea == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
				 errmsg("bfinstr must not be NULL")));

	/*
	 * The globals below hold one call's state, so a nested call, for example
	 * from the transform, would overwrite and then clear the outer call's.
	 */
	if (yb_backfill_timestamp != 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("yb_backfill_online_schema_change_chunk cannot be called recursively")));

	/* Allocated before SPI_connect so that it survives SPI_finish. */
	YbPgExecOutParam *outParam = YbCreateExecOutParam();

	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	char	   *partitionKeyHex = HexEncodeBytea(partitionKeyBytea);
	char	   *bfinstrHex = HexEncodeBytea(bfinstrBytea);

	/*
	 * The target is locked in the mode the INSERT takes, and every lock is
	 * kept until the end of the transaction, so the INSERT never upgrades a
	 * lock and the relations cannot change between deparsing and execution.
	 */
	Relation	srcRel = table_open(srcOid, AccessShareLock);
	Oid			saveUserId;
	int			saveSecContext;

	GetUserIdAndSecContext(&saveUserId, &saveSecContext);
	SetUserIdAndSecContext(srcRel->rd_rel->relowner,
						   saveSecContext | SECURITY_RESTRICTED_OPERATION);

	int			saveNestLevel = NewGUCNestLevel();

	Relation	dstRel = table_open(dstOid, RowExclusiveLock);

	char	   *srcQual = YbOscQuoteQualifiedRelName(srcRel);
	char	   *dstQual = YbOscQuoteQualifiedRelName(dstRel);
	char	   *dstCols = BuildDstColumnList(RelationGetDescr(dstRel));

	YbOscTransform *xf = YbOscCompileTransform(transformOid, srcRel,
											   dstRel);

	/*
	 * From here on, names resolve in pg_catalog first and pg_temp last, so
	 * ruleutils schema-qualifies every other name it deparses and planning
	 * cannot bind one to an object created elsewhere.  row_security is off so
	 * that a row-level security policy that applies to the owner fails the
	 * copy instead of silently leaving rows out.
	 */
	(void) set_config_option("search_path", "pg_catalog, pg_temp",
							 PGC_USERSET, PGC_S_SESSION,
							 GUC_ACTION_SAVE, true, 0, false);
	(void) set_config_option("row_security", "off",
							 PGC_USERSET, PGC_S_SESSION,
							 GUC_ACTION_SAVE, true, 0, false);
	YbOscForceSpeculativePl();

	char	   *selectList = YbOscTransformDeparse(xf, srcRel, dstRel);
	char	   *fromList = psprintf("%s AS s CROSS JOIN LATERAL %s AS t", srcQual,
									YbOscTransformDeparseCall(xf, srcRel));

	table_close(dstRel, NoLock);
	table_close(srcRel, NoLock);

	/*
	 * No LIMIT: pggate applies the tablet scope, row limit and resume key to
	 * the scan.  No ORDER BY: the scan must run in DocDB storage order so that
	 * it can stop at the row limit and resume from the returned key, and an
	 * ORDER BY would at best restate that order and at worst add a Sort that
	 * drains the whole tablet in one call.  Index backfill relies on the same
	 * unordered scan.
	 */
	char	   *sql = psprintf("INSERT INTO %s (%s) SELECT %s FROM %s",
							   dstQual, dstCols, selectList, fromList);

	SPIPlanPtr	plan = SPI_prepare(sql, 0, NULL);

	if (plan == NULL)
		ereport(ERROR, (errmsg("SPI_prepare failed for backfill INSERT ... SELECT")));

	int64		rowsProcessed = 0;

	PG_TRY();
	{
		yb_backfill_timestamp = (uint64_t) readTimeHt;
		yb_osc_backfill_partition_key = partitionKeyHex;
		yb_osc_backfill_bfinstr = bfinstrHex;
		yb_osc_backfill_out_param = outParam;
		yb_osc_backfill_outer_started = false;

		int			rc = SPI_execute_plan(plan, NULL, NULL, false /* read_only */ , 0);

		yb_backfill_timestamp = 0;
		yb_osc_backfill_partition_key = NULL;
		yb_osc_backfill_bfinstr = NULL;
		yb_osc_backfill_out_param = NULL;
		yb_osc_backfill_outer_started = false;

		if (rc != SPI_OK_INSERT)
			ereport(ERROR,
					(errmsg("online schema change backfill INSERT ... SELECT failed (rc=%d)", rc)));

		rowsProcessed = SPI_processed;
	}
	PG_CATCH();
	{
		yb_backfill_timestamp = 0;
		yb_osc_backfill_partition_key = NULL;
		yb_osc_backfill_bfinstr = NULL;
		yb_osc_backfill_out_param = NULL;
		yb_osc_backfill_outer_started = false;
		PG_RE_THROW();
	}
	PG_END_TRY();

	SPI_finish();

	/* On error, transaction abort restores both. */
	AtEOXact_GUC(false, saveNestLevel);
	SetUserIdAndSecContext(saveUserId, saveSecContext);

	PG_RETURN_DATUM(BuildChunkResult(fcinfo, rowsProcessed, outParam));
}
