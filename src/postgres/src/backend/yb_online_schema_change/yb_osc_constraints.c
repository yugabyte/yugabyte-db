/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_constraints.c
 *    Carrying the source table's foreign keys over to the target.
 *
 *    The target has no foreign keys while the copy runs.  An RI check on the
 *    copy's INSERT ... SELECT would run a nested statement, which
 *    ybOscRejectNestedTableAccess rejects.  The parent-side actions of a
 *    foreign key would also change the target outside the capture trigger,
 *    whose tombstones are what keep a row that the copy has not reached yet
 *    from reappearing.
 *
 *    So the source enforces its foreign keys during the copy, and the
 *    resulting writes reach the target through the trigger.  Afterwards the
 *    constraints are added to the target and validated, and at cutover they
 *    are dropped from the source.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_constraints.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_class.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "yb_osc_internal.h"


typedef struct YbOscForeignKey
{
	char	   *conname;
	char	   *condef;			/* pg_get_constraintdef() output */
	bool		convalidated;
} YbOscForeignKey;


/*
 * Read the foreign keys of relOid.  pg_get_constraintdef refers to columns by
 * name, so its output also applies to a target whose column order differs.
 */
static List *
CollectForeignKeys(Oid relOid)
{
	List	   *result = NIL;
	MemoryContext callerCtx = CurrentMemoryContext;

	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	const char *sql =
		"SELECT conname, pg_catalog.pg_get_constraintdef(oid), convalidated "
		"FROM pg_catalog.pg_constraint "
		"WHERE conrelid = $1 AND contype = 'f' "
		"ORDER BY conname";

	Oid			argtypes[1] = {OIDOID};
	Datum		values[1] = {ObjectIdGetDatum(relOid)};

	if (SPI_execute_with_args(sql, 1, argtypes, values, NULL, true, 0) != SPI_OK_SELECT)
		ereport(ERROR, (errmsg("failed to query foreign keys of relation %u", relOid)));

	for (uint64 i = 0; i < SPI_processed; i++)
	{
		HeapTuple	tuple = SPI_tuptable->vals[i];
		TupleDesc	tupdesc = SPI_tuptable->tupdesc;
		bool		isnull = false;

		Datum		nameDatum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
		Datum		defDatum = SPI_getbinval(tuple, tupdesc, 2, &isnull);
		Datum		validDatum = SPI_getbinval(tuple, tupdesc, 3, &isnull);

		MemoryContext oldctx = MemoryContextSwitchTo(callerCtx);
		YbOscForeignKey *fk = palloc(sizeof(YbOscForeignKey));

		fk->conname = pstrdup(NameStr(*DatumGetName(nameDatum)));
		fk->condef = TextDatumGetCString(defDatum);
		fk->convalidated = DatumGetBool(validDatum);
		result = lappend(result, fk);
		MemoryContextSwitchTo(oldctx);
	}

	SPI_finish();

	return result;
}

static YbOscForeignKey *
FindForeignKeyByName(List *fkeys, const char *conname)
{
	ListCell   *lc;

	foreach(lc, fkeys)
	{
		YbOscForeignKey *fk = (YbOscForeignKey *) lfirst(lc);

		if (strcmp(fk->conname, conname) == 0)
			return fk;
	}
	return NULL;
}

static char *
QuoteQualifiedRelNameByOid(Oid relOid)
{
	return quote_qualified_identifier(get_namespace_name(get_rel_namespace(relOid)),
									  get_rel_name(relOid));
}

static void
ExecAlterTable(const char *sql)
{
	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	if (SPI_execute(sql, false, 0) != SPI_OK_UTILITY)
		ereport(ERROR, (errmsg("online schema change constraint DDL failed: %s", sql)));

	SPI_finish();
}

/*
 * Add the source's foreign keys to the target as NOT VALID, then validate
 * them.  Neither step blocks writes for long: ADD ... NOT VALID does no scan,
 * and VALIDATE CONSTRAINT takes only ShareUpdateExclusiveLock on the target
 * and RowShareLock on the referenced table.  A validating ADD CONSTRAINT
 * would hold ShareRowExclusiveLock on both tables for a full scan.
 *
 * PostgreSQL refuses NOT VALID foreign keys on a partitioned table, so a
 * partitioned target gets the validating ADD CONSTRAINT instead, and writes
 * to the referenced table block until its scan finishes.
 *
 * The validation also checks the copy and the trigger, so it is not skipped.
 * A constraint that is NOT VALID on the source stays unvalidated.  Work
 * already done is skipped, so a failed call can be retried.
 */
void
YbOscPromoteForeignKeys(Oid srcOid, Oid dstOid)
{
	List	   *srcFkeys = CollectForeignKeys(srcOid);
	List	   *dstFkeys = CollectForeignKeys(dstOid);
	char	   *dstQual = QuoteQualifiedRelNameByOid(dstOid);
	bool		dstPartitioned = get_rel_relkind(dstOid) == RELKIND_PARTITIONED_TABLE;
	ListCell   *lc;

	foreach(lc, srcFkeys)
	{
		YbOscForeignKey *fk = (YbOscForeignKey *) lfirst(lc);
		YbOscForeignKey *existing = FindForeignKeyByName(dstFkeys, fk->conname);
		const char *quotedName = quote_identifier(fk->conname);

		if (dstPartitioned && !fk->convalidated)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot carry NOT VALID foreign key \"%s\" over to partitioned "
							"target table \"%s\"",
							fk->conname, get_rel_name(dstOid)),
					 errhint("Validate the constraint on the source first.")));

		if (existing == NULL)
		{
			if (dstPartitioned)
			{
				ExecAlterTable(psprintf("ALTER TABLE %s ADD CONSTRAINT %s %s",
										dstQual, quotedName, fk->condef));
				continue;
			}

			/* condef already ends in NOT VALID if the source's is. */
			ExecAlterTable(psprintf("ALTER TABLE %s ADD CONSTRAINT %s %s%s",
									dstQual, quotedName, fk->condef,
									fk->convalidated ? " NOT VALID" : ""));
		}
		else if (existing->convalidated || !fk->convalidated)
			continue;			/* already in its final state */

		if (fk->convalidated)
			ExecAlterTable(psprintf("ALTER TABLE %s VALIDATE CONSTRAINT %s",
									dstQual, quotedName));
	}
}

/*
 * Error out unless every foreign key of the source exists on the target, and
 * is validated if the source's is.  Checked at cutover so that the table that
 * takes the source's name enforces at least what the source did.
 */
void
YbOscCheckForeignKeysPromoted(Oid srcOid, Oid dstOid)
{
	List	   *srcFkeys = CollectForeignKeys(srcOid);
	List	   *dstFkeys = CollectForeignKeys(dstOid);
	ListCell   *lc;

	foreach(lc, srcFkeys)
	{
		YbOscForeignKey *fk = (YbOscForeignKey *) lfirst(lc);
		YbOscForeignKey *existing = FindForeignKeyByName(dstFkeys, fk->conname);

		if (existing == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("target table \"%s\" is missing foreign key \"%s\"",
							get_rel_name(dstOid), fk->conname),
					 errhint("Run yb_wait_mirror_table, which adds it, before finishing the "
							 "online schema change.")));

		if (fk->convalidated && !existing->convalidated)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("foreign key \"%s\" on target table \"%s\" is not validated",
							fk->conname, get_rel_name(dstOid)),
					 errhint("Run yb_wait_mirror_table, which adds it, before finishing the "
							 "online schema change.")));
	}
}

/*
 * Drop the foreign keys of relOid.  Used on the source at cutover: a foreign
 * key follows its table through a rename, so the old source would otherwise
 * keep restricting or cascading changes to the referenced tables.
 */
void
YbOscDropForeignKeys(Oid relOid)
{
	List	   *fkeys = CollectForeignKeys(relOid);
	char	   *relQual = QuoteQualifiedRelNameByOid(relOid);
	ListCell   *lc;

	foreach(lc, fkeys)
	{
		YbOscForeignKey *fk = (YbOscForeignKey *) lfirst(lc);

		ExecAlterTable(psprintf("ALTER TABLE %s DROP CONSTRAINT %s",
								relQual, quote_identifier(fk->conname)));
	}
}
