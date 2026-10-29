/*--------------------------------------------------------------------------------------------------
 *
 * yb_osc_clone.c
 *    yb_online_schema_change_clone_partitions: recreate a source table's
 *    partition hierarchy under an empty partitioned target.  Optional: the job
 *    writes through the target root's tuple routing, so the target may have
 *    any shape.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/backend/yb_online_schema_change/yb_osc_clone.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include <inttypes.h>

#include "access/genam.h"
#include "access/table.h"
#include "access/xact.h"
#include "catalog/partition.h"
#include "catalog/pg_class.h"
#include "catalog/pg_inherits.h"
#include "commands/tablespace.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "pg_yb_utils.h"
#include "storage/lmgr.h"
#include "utils/builtins.h"
#include "utils/fmgrprotos.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "yb_osc_internal.h"


PG_FUNCTION_INFO_V1(yb_online_schema_change_clone_partitions);


static char *
QuoteQualifiedRelNameByOid(Oid relOid)
{
	return quote_qualified_identifier(get_namespace_name(get_rel_namespace(relOid)),
									  get_rel_name(relOid));
}

static int
OidPosition(List *oids, Oid oid)
{
	int			pos = 0;
	ListCell   *lc;

	foreach(lc, oids)
	{
		if (lfirst_oid(lc) == oid)
			return pos;
		pos++;
	}
	return -1;
}

/*
 * Refuses an overlong name rather than truncating it: two long sibling names
 * truncated to the same identifier would fail with a confusing "relation
 * already exists".
 */
static char *
SuffixedName(const char *relname, const char *suffix)
{
	char	   *name = psprintf("%s%s", relname, suffix);

	if (strlen(name) >= NAMEDATALEN)
		ereport(ERROR,
				(errcode(ERRCODE_NAME_TOO_LONG),
				 errmsg("cloned name \"%s\" is too long", name),
				 errhint("Use a shorter suffix.")));
	return name;
}

static void
ExecDdl(const char *sql)
{
	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	if (SPI_execute(sql, false, 0) != SPI_OK_UTILITY)
		ereport(ERROR, (errmsg("online schema change clone DDL failed: %s", sql)));

	SPI_finish();
	CommandCounterIncrement();
}

/* Same key column names, types and HASH/ASC/DESC options, in order. */
static bool
SamePrimaryKey(Oid aOid, Oid bOid)
{
	Relation	a = table_open(aOid, NoLock);
	Relation	b = table_open(bOid, NoLock);
	Oid			aPk = RelationGetPrimaryKeyIndex(a);
	Oid			bPk = RelationGetPrimaryKeyIndex(b);
	bool		same = OidIsValid(aPk) && OidIsValid(bPk);

	if (same)
	{
		Relation	aIdx = index_open(aPk, AccessShareLock);
		Relation	bIdx = index_open(bPk, AccessShareLock);
		int			nkeys = IndexRelationGetNumberOfKeyAttributes(aIdx);

		same = nkeys == IndexRelationGetNumberOfKeyAttributes(bIdx);
		for (int i = 0; same && i < nkeys; i++)
		{
			Form_pg_attribute aAtt =
				TupleDescAttr(RelationGetDescr(a), aIdx->rd_index->indkey.values[i] - 1);
			Form_pg_attribute bAtt =
				TupleDescAttr(RelationGetDescr(b), bIdx->rd_index->indkey.values[i] - 1);

			same = strcmp(NameStr(aAtt->attname), NameStr(bAtt->attname)) == 0 &&
				aAtt->atttypid == bAtt->atttypid &&
				aIdx->rd_indoption[i] == bIdx->rd_indoption[i];
		}
		index_close(aIdx, AccessShareLock);
		index_close(bIdx, AccessShareLock);
	}

	table_close(a, NoLock);
	table_close(b, NoLock);
	return same;
}

/*
 * The split clause that gives the clone of leafOid the same tablet layout, as
 * far as the clone's key allows, or NULL.  The clone inherits its primary key
 * from dstParentOid, so the tablet count carries over only between
 * hash-sharded tables, and the split points (key values) only to a clone
 * with the same key.
 */
static char *
SplitClause(Oid leafOid, Oid dstParentOid)
{
	YbcTableProperties src = YbGetTablePropertiesById(leafOid);
	YbcTableProperties dst = YbGetTablePropertiesById(dstParentOid);

	if (src->is_colocated || dst->is_colocated)
		return NULL;

	if (dst->num_hash_key_columns > 0)
		return src->num_hash_key_columns > 0 ?
			psprintf("SPLIT INTO %" PRIu64 " TABLETS", src->num_tablets) : NULL;

	if (src->num_hash_key_columns > 0 || src->num_tablets <= 1 ||
		!SamePrimaryKey(leafOid, dstParentOid))
		return NULL;

	char	   *clause =
		DatumGetCString(DirectFunctionCall1(yb_get_range_split_clause,
											ObjectIdGetDatum(leafOid)));

	return clause[0] != '\0' ? clause : NULL;
}

static Oid
CloneNode(Oid srcOid, Oid dstParentOid, const char *suffix)
{
	Oid			nspOid = get_rel_namespace(srcOid);
	char	   *newName = SuffixedName(get_rel_name(srcOid), suffix);
	StringInfoData buf;

	/* Allocated before SPI_connect so it outlives SPI_finish. */
	initStringInfo(&buf);

	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	const char *sql =
		"SELECT pg_catalog.pg_get_expr(c.relpartbound, c.oid), "
		"       CASE WHEN c.relkind = 'p' THEN pg_catalog.pg_get_partkeydef(c.oid) END, "
		"       pg_catalog.array_to_string(c.reloptions, ', '), "
		"       c.reltablespace "
		"FROM pg_catalog.pg_class c WHERE c.oid = $1";
	Oid			argtypes[1] = {OIDOID};
	Datum		values[1] = {ObjectIdGetDatum(srcOid)};

	if (SPI_execute_with_args(sql, 1, argtypes, values, NULL, true, 1) != SPI_OK_SELECT ||
		SPI_processed != 1)
		ereport(ERROR, (errmsg("failed to read the definition of relation %u", srcOid)));

	TupleDesc	tupdesc = SPI_tuptable->tupdesc;
	HeapTuple	tuple = SPI_tuptable->vals[0];
	bool		isnull;
	Datum		bound = SPI_getbinval(tuple, tupdesc, 1, &isnull);

	if (isnull)
		ereport(ERROR,
				(errmsg("relation \"%s\" has no partition bound", get_rel_name(srcOid))));

	appendStringInfo(&buf, "CREATE TABLE %s PARTITION OF %s %s",
					 quote_qualified_identifier(get_namespace_name(nspOid), newName),
					 QuoteQualifiedRelNameByOid(dstParentOid),
					 TextDatumGetCString(bound));

	Datum		partkeydef = SPI_getbinval(tuple, tupdesc, 2, &isnull);

	if (!isnull)
		appendStringInfo(&buf, " PARTITION BY %s", TextDatumGetCString(partkeydef));

	Datum		reloptions = SPI_getbinval(tuple, tupdesc, 3, &isnull);

	if (!isnull)
		appendStringInfo(&buf, " WITH (%s)", TextDatumGetCString(reloptions));

	Oid			tablespaceOid = DatumGetObjectId(SPI_getbinval(tuple, tupdesc, 4, &isnull));

	if (OidIsValid(tablespaceOid))
		appendStringInfo(&buf, " TABLESPACE %s",
						 quote_identifier(get_tablespace_name(tablespaceOid)));

	SPI_finish();

	if (get_rel_relkind(srcOid) == RELKIND_RELATION)
	{
		char	   *split = SplitClause(srcOid, dstParentOid);

		if (split != NULL)
			appendStringInfo(&buf, " %s", split);
	}

	ExecDdl(buf.data);

	Oid			newOid = get_relname_relid(newName, nspOid);

	if (!OidIsValid(newOid))
		ereport(ERROR, (errmsg("cloned partition \"%s\" not found after creation", newName)));
	return newOid;
}

/*
 * Recreate on dstOid the indexes local to srcOid; an index that is a
 * partition of a parent's index comes from the target's own indexes.
 * pg_get_indexdef names the source leaf, so only the text from USING onwards
 * is reused.
 */
static void
CloneLocalIndexes(Oid srcOid, Oid dstOid, const char *suffix)
{
	List	   *ddls = NIL;
	MemoryContext callerCtx = CurrentMemoryContext;
	char	   *dstQual = QuoteQualifiedRelNameByOid(dstOid);

	if (SPI_connect() != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("SPI_connect failed")));

	const char *sql =
		"SELECT ic.relname, i.indisunique, pg_catalog.pg_get_indexdef(i.indexrelid) "
		"FROM pg_catalog.pg_index i JOIN pg_catalog.pg_class ic ON ic.oid = i.indexrelid "
		"WHERE i.indrelid = $1 AND NOT i.indisprimary "
		"  AND NOT EXISTS (SELECT 1 FROM pg_catalog.pg_inherits h "
		"                  WHERE h.inhrelid = i.indexrelid) "
		"ORDER BY ic.relname";
	Oid			argtypes[1] = {OIDOID};
	Datum		values[1] = {ObjectIdGetDatum(srcOid)};

	if (SPI_execute_with_args(sql, 1, argtypes, values, NULL, true, 0) != SPI_OK_SELECT)
		ereport(ERROR, (errmsg("failed to read the indexes of relation %u", srcOid)));

	for (uint64 i = 0; i < SPI_processed; i++)
	{
		TupleDesc	tupdesc = SPI_tuptable->tupdesc;
		HeapTuple	tuple = SPI_tuptable->vals[i];
		bool		isnull;
		const char *idxname = NameStr(*DatumGetName(SPI_getbinval(tuple, tupdesc, 1, &isnull)));
		bool		unique = DatumGetBool(SPI_getbinval(tuple, tupdesc, 2, &isnull));
		const char *indexdef = TextDatumGetCString(SPI_getbinval(tuple, tupdesc, 3, &isnull));
		const char *tail = strstr(indexdef, " USING ");

		if (tail == NULL)
			ereport(ERROR,
					(errmsg("cannot clone index \"%s\": unexpected definition \"%s\"",
							idxname, indexdef)));

		MemoryContext oldctx = MemoryContextSwitchTo(callerCtx);

		ddls = lappend(ddls,
					   psprintf("CREATE %sINDEX NONCONCURRENTLY %s ON %s%s",
								unique ? "UNIQUE " : "",
								quote_identifier(SuffixedName(idxname, suffix)),
								dstQual, tail));
		MemoryContextSwitchTo(oldctx);
	}

	SPI_finish();

	ListCell   *lc;

	foreach(lc, ddls)
		ExecDdl((const char *) lfirst(lc));
}

/*
 * yb_online_schema_change_clone_partitions(source_table, target_table, suffix,
 *                            clone_local_indexes)
 *
 * For every partition of source_table, create a partition of the
 * corresponding target node named after it plus suffix, with the same bound,
 * sub-partitioning, reloptions, tablespace and tablet layout.  The primary
 * key is inherited from the target.  Ordinary DDL in the caller's
 * transaction, so an error rolls back every clone.
 */
Datum
yb_online_schema_change_clone_partitions(PG_FUNCTION_ARGS)
{
	Oid			srcOid = PG_GETARG_OID(0);
	Oid			dstOid = PG_GETARG_OID(1);
	const char *suffix = text_to_cstring(PG_GETARG_TEXT_PP(2));
	bool		cloneLocalIndexes = PG_GETARG_BOOL(3);

	if (get_rel_relkind(srcOid) != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("source table \"%s\" is not partitioned", get_rel_name(srcOid))));
	if (get_rel_relkind(dstOid) != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("target table \"%s\" is not partitioned", get_rel_name(dstOid))));

	/*
	 * The lock CREATE TABLE ... PARTITION OF takes anyway; it keeps the
	 * emptiness check below valid.
	 */
	LockRelationOid(dstOid, AccessExclusiveLock);

	if (find_inheritance_children(dstOid, NoLock) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("target table \"%s\" already has partitions", get_rel_name(dstOid))));

	/*
	 * Parents precede their children in srcNodes, so a node's parent has been
	 * cloned before the node; dstNodes holds the clones at the same positions.
	 */
	List	   *srcNodes = YbOscHierarchyNodes(srcOid, AccessShareLock);
	List	   *dstNodes = list_make1_oid(dstOid);
	ListCell   *lc;

	for_each_from(lc, srcNodes, 1)
	{
		Oid			nodeOid = lfirst_oid(lc);
		Oid			parentOid = get_partition_parent(nodeOid, false);
		int			parentPos = OidPosition(srcNodes, parentOid);

		if (parentPos < 0)
			ereport(ERROR,
					(errmsg("parent of partition \"%s\" is not in the hierarchy",
							get_rel_name(nodeOid))));

		Oid			newOid = CloneNode(nodeOid, list_nth_oid(dstNodes, parentPos), suffix);

		dstNodes = lappend_oid(dstNodes, newOid);

		if (cloneLocalIndexes && get_rel_relkind(nodeOid) == RELKIND_RELATION)
			CloneLocalIndexes(nodeOid, newOid, suffix);
	}

	PG_RETURN_VOID();
}
