/*--------------------------------------------------------------------------
 *
 * ybginget.c
 *	  fetch tuples from a Yugabyte GIN scan.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *			src/backend/access/ybgin/ybginget.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/genam.h"
#include "access/htup.h"
#include "access/relscan.h"
#include "access/sdir.h"
#include "access/sysattr.h"
#include "access/yb_scan.h"
#include "access/ybgin.h"
#include "access/ybgin_private.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_opfamily.h"
#include "catalog/pg_type.h"
#include "nodes/makefuncs.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/selfuncs.h"
#include "utils/yb_like_support.h"

#include "pg_yb_utils.h"
#include "yb/yql/pggate/ybc_pggate.h"

/* Copied from ginget.c. */
#define GinIsVoidRes(s)		( ((GinScanOpaque) scan->opaque)->isVoidRes )

#define TSVECTOR_GIN_FAM_OID	((Oid) 3659)

/*
 * Parts copied from ginget.c.  Take the code right under label
 * restartScanEntry that initializes entry.
 */
static void
startScanEntry(GinScanEntry entry)
{
	entry->buffer = InvalidBuffer;
	ItemPointerSetMin(&entry->curItem);
	entry->offset = InvalidOffsetNumber;
	if (entry->list)
		pfree(entry->list);
	entry->list = NULL;
	entry->nlist = 0;
	entry->matchBitmap = NULL;
	entry->matchResult = NULL;
	entry->reduceResult = false;
	entry->predictNumberResult = 0;
}

/*
 * Comparison function for scan entry indexes. Sorts by predictNumberResult,
 * least frequent items first.
 *
 * Copied from ginget.c.
 */
static int
entryIndexByFrequencyCmp(const void *a1, const void *a2, void *arg)
{
	const GinScanKey key = (const GinScanKey) arg;
	int			i1 = *(const int *) a1;
	int			i2 = *(const int *) a2;
	uint32		n1 = key->scanEntry[i1]->predictNumberResult;
	uint32		n2 = key->scanEntry[i2]->predictNumberResult;

	if (n1 < n2)
		return -1;
	else if (n1 == n2)
		return 0;
	else
		return 1;
}

/*
 * Copied from ginget.c with the only difference being the lack of parameter
 * ginstate, which isn't used there either.
 */
static void
startScanKey(GinScanOpaque so, GinScanKey key)
{
	MemoryContext oldCtx = GetCurrentMemoryContext();
	int			i;
	int			j;
	int		   *entryIndexes;

	ItemPointerSetMin(&key->curItem);
	key->curItemMatches = false;
	key->recheckCurItem = false;
	key->isFinished = false;

	/*
	 * Divide the entries into two distinct sets: required and additional.
	 * Additional entries are not enough for a match alone, without any items
	 * from the required set, but are needed by the consistent function to
	 * decide if an item matches. When scanning, we can skip over items from
	 * additional entries that have no corresponding matches in any of the
	 * required entries. That speeds up queries like "frequent & rare"
	 * considerably, if the frequent term can be put in the additional set.
	 *
	 * There can be many legal ways to divide them entries into these two
	 * sets. A conservative division is to just put everything in the required
	 * set, but the more you can put in the additional set, the more you can
	 * skip during the scan. To maximize skipping, we try to put as many
	 * frequent items as possible into additional, and less frequent ones into
	 * required. To do that, sort the entries by frequency
	 * (predictNumberResult), and put entries into the required set in that
	 * order, until the consistent function says that none of the remaining
	 * entries can form a match, without any items from the required set. The
	 * rest go to the additional set.
	 */
	if (key->nentries > 1)
	{
		MemoryContextSwitchTo(so->tempCtx);

		entryIndexes = (int *) palloc(sizeof(int) * key->nentries);
		for (i = 0; i < key->nentries; i++)
			entryIndexes[i] = i;
		qsort_arg(entryIndexes, key->nentries, sizeof(int),
				  entryIndexByFrequencyCmp, key);

		for (i = 0; i < key->nentries - 1; i++)
		{
			/* Pass all entries <= i as FALSE, and the rest as MAYBE */
			for (j = 0; j <= i; j++)
				key->entryRes[entryIndexes[j]] = GIN_FALSE;
			for (j = i + 1; j < key->nentries; j++)
				key->entryRes[entryIndexes[j]] = GIN_MAYBE;

			if (key->triConsistentFn(key) == GIN_FALSE)
				break;
		}
		/* i is now the last required entry. */

		MemoryContextSwitchTo(so->keyCtx);

		key->nrequired = i + 1;
		key->nadditional = key->nentries - key->nrequired;
		key->requiredEntries = palloc(key->nrequired * sizeof(GinScanEntry));
		key->additionalEntries = palloc(key->nadditional * sizeof(GinScanEntry));

		j = 0;
		for (i = 0; i < key->nrequired; i++)
			key->requiredEntries[i] = key->scanEntry[entryIndexes[j++]];
		for (i = 0; i < key->nadditional; i++)
			key->additionalEntries[i] = key->scanEntry[entryIndexes[j++]];

		/* clean up after consistentFn calls (also frees entryIndexes) */
		MemoryContextReset(so->tempCtx);
	}
	else
	{
		MemoryContextSwitchTo(so->keyCtx);

		key->nrequired = 1;
		key->nadditional = 0;
		key->requiredEntries = palloc(1 * sizeof(GinScanEntry));
		key->requiredEntries[0] = key->scanEntry[0];
	}
	MemoryContextSwitchTo(oldCtx);
}

/*
 * Parts copied from ginget.c.  Don't bother copying GinFuzzySearchLimit code
 * since it's not supported.
 */
static void
startScan(IndexScanDesc scan)
{
	GinScanOpaque so = (GinScanOpaque) scan->opaque;
	uint32		i;

	for (i = 0; i < so->totalentries; i++)
		startScanEntry(so->entries[i]);

	/* Don't support GinFuzzySearchLimit. */

	/*
	 * Now that we have the estimates for the entry frequencies, finish
	 * initializing the scan keys.
	 */
	for (i = 0; i < so->nkeys; i++)
		startScanKey(so, so->keys + i);
}

/*
 * Set up the scan keys, and check for unsatisfiable query.
 *
 * Parts copied from ginget.c gingetbitmap.  Since ybgin does not use bitmap
 * scan, things are much different.  gingetbitmap iterates over the resulting
 * tuples, but ybgingettuple will be called once for each result tuple.  Only
 * take the setup part of gingetbitmap.
 */
static bool
ybginGetScanKeys(IndexScanDesc scan)
{
	GinScanOpaque so = (GinScanOpaque) scan->opaque;

	ginFreeScanKeys(so);		/* there should be no keys yet, but just to be
								 * sure */
	ginNewScanKey(scan);

	if (GinIsVoidRes(scan))
		return false;

	/*
	 * Categorize scan entries to required and additional.
	 */
	startScan(scan);

	return true;
}

/*
 * Generate a Const node of text type from a C string.
 *
 * Parts copied from string_to_const.
 */
static Const *
text_to_const(Datum conval, Oid colloid)
{
	Oid			datatype = TEXTOID;
	int			constlen = -1;

	return makeConst(datatype, -1, colloid, constlen,
					 conval, false, false);
}

/*
 * Try to generate a string to serve as an exclusive upperbound for matching
 * strings with the given prefix.  If successful, return a palloc'd string in
 * the form of a Const node; else, return NULL.
 */
static Const *
get_greaterstr(Datum prefix, Oid datatype, Oid colloid)
{
	Const	   *prefix_const;
	FmgrInfo	ltproc;
	Oid			opfamily;
	Oid			oproid;

	/* yb_make_greater_string cannot accurately handle non-C collations. */
	if (!lc_collate_is_c(colloid))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot handle ybgin scans with prefix on non-C collation %u",
						colloid)));

	/*
	 * For now, hardcode to assume type is text.  This is true for the four
	 * native postgres ybgin opclasses, but it may no longer be true when
	 * supporting extensions like btree_gin.  This assumption makes finding
	 * opfamily and operator easier.
	 */
	if (datatype != TEXTOID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot handle ybgin scans with prefix on key type %u",
						datatype)));

	opfamily = TEXT_LSM_FAM_OID;
	oproid = get_opfamily_member(opfamily, datatype, datatype,
								 BTLessStrategyNumber);
	if (oproid == InvalidOid)
		elog(ERROR, "no < operator for opfamily %u", opfamily);
	fmgr_info(get_opcode(oproid), &ltproc);
	prefix_const = text_to_const(prefix, colloid);
	return yb_make_greater_string(prefix_const, &ltproc, colloid);
}

static void
ybginSetupBindsForPrefix(TupleDesc tupdesc, YbginScanOpaque ybso,
						 GinScanEntry entry)
{
	Const	   *greaterstr;
	GinScanOpaque so = (GinScanOpaque) ybso;
	Oid			colloid;
	Oid			typoid;
	YBCPgExpr	expr_start,
				expr_end;

	colloid = so->ginstate.supportCollation[0];
	typoid = TupleDescAttr(tupdesc, 0)->atttypid;

	expr_start = YBCNewConstant(ybso->handle,
								typoid,
								colloid,
								entry->queryKey,
								false /* is_null */);

	greaterstr = get_greaterstr((Datum) entry->queryKey,
								typoid,
								colloid);
	if (greaterstr)
	{
		expr_end = YBCNewConstant(ybso->handle,
								  typoid,
								  colloid,
								  greaterstr->constvalue,
								  false /* is_null */);
		HandleYBStatus(YBCPgDmlBindColumnCondBetween(ybso->handle,
													 1 /* attr_num */,
													 expr_start,
													 true,
													 expr_end,
													 false));
		pfree(greaterstr);
	}
	else
		HandleYBStatus(YBCPgDmlBindColumnCondBetween(ybso->handle,
													 1 /* attr_num */,
													 expr_start,
													 true,
													 NULL /* attr_value_end */,
													 true));
}

static void
ybginSetupBindsForPartialMatch(TupleDesc tupdesc, YbginScanOpaque ybso,
							   GinScanEntry entry)
{
	GinScanOpaque so = (GinScanOpaque) ybso;

	/*
	 * For now, assume partial match always means prefix match.  In the
	 * future, this should be handled by a new support function, similar to
	 * the existing support function comparePartial.
	 *
	 * TODO(jason): don't assume one column when multicolumn is supported.
	 */
	if (so->ginstate.index->rd_opfamily[0] != TSVECTOR_GIN_FAM_OID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("unsupported ybgin index scan"),
				 errdetail("Partial match with ybgin index method"
						   " currently only supports opfamily %u: got %u.",
						   TSVECTOR_GIN_FAM_OID,
						   so->ginstate.index->rd_opfamily[0]),
				 errhint("Turn off index scan using"
						 " \"SET enable_indexscan TO false\".")));
	ybginSetupBindsForPrefix(tupdesc, ybso, entry);
}

/*
 * Add binds for the select.
 */
static void
ybginSetupBinds(IndexScanDesc scan)
{
	GinScanEntry entry;
	GinScanKey	key;
	GinScanOpaque so = (GinScanOpaque) scan->opaque;
	TupleDesc	tupdesc;
	YbginScanOpaque ybso = (YbginScanOpaque) scan->opaque;

	/*
	 * For now, only handle single-key scans.  Multiple keys are possible even
	 * if multicolumn is disabled by specifiying the same column in multiple
	 * conditions (e.g. v @@ 'abc' and v @@ 'bcd').
	 */
	if (so->nkeys != 1)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("unsupported ybgin index scan"),
				 errdetail("ybgin index method cannot use"
						   " more than one scan key: got %d.",
						   so->nkeys),
				 errhint("Consider rewriting the query with INTERSECT and"
						 " UNION.")));
	key = &so->keys[0];

	/*
	 * For now, only handle the default search mode.
	 */
	if (key->searchMode != GIN_SEARCH_MODE_DEFAULT)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("unsupported ybgin index scan"),
				 errdetail("ybgin index method does not support"
						   " non-default search mode: %s.",
						   ybginSearchModeToString(key->searchMode))));

	/*
	 * For now, only handle the case where there's one required scan entry
	 */
	if (key->nrequired != 1)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("unsupported ybgin index scan"),
				 errdetail("ybgin index method cannot use"
						   " more than one required scan entry: got %d.",
						   key->nrequired)));
	entry = key->requiredEntries[0];


	/* For now, don't handle null entries. */
	if (entry->queryCategory != GIN_CAT_NORM_KEY)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("unsupported ybgin index scan"),
				 errdetail("ybgin index method does not support"
						   " non-normal null category: %s.",
						   ybginNullCategoryToString(entry->queryCategory))));

	/* Bind to the index because why else would we do an index scan? */
	tupdesc = RelationGetDescr(scan->indexRelation);

	if (entry->isPartialMatch)
	{
		ybginSetupBindsForPartialMatch(tupdesc, ybso, entry);
	}
	else
	{
		YBCPgExpr	expr;

		/* Bind the one scan entry to the index column. */
		expr = YBCNewConstant(ybso->handle,
							  TupleDescAttr(tupdesc, 0)->atttypid,
							  so->ginstate.supportCollation[0],
							  entry->queryKey,
							  false /* is_null */);
		HandleYBStatus(YBCPgDmlBindColumn(ybso->handle,
										  1 /* attr_num */,
										  expr));
	}
}

/*
 * Add targets for the select.
 */
static void
ybginSetupTargets(IndexScanDesc scan)
{
	TupleDesc	tupdesc;
	YbginScanOpaque ybso = (YbginScanOpaque) scan->opaque;

	/*
	 * We don't support IndexOnlyScan, which would directly target the index
	 * table.  Therefore, as an IndexScan, target the base table.  Change this
	 * if we ever support ybgin IndexOnlyScan.
	 */
	tupdesc = RelationGetDescr(scan->heapRelation);

	/*
	 * IndexScan needs to get base ctids from the index table to pass as binds
	 * to the base table.  This is handled in the pggate layer.
	 */
	YbDmlAppendTargetSystem(YBIdxBaseTupleIdAttributeNumber, ybso->handle);
	/*
	 * For scans that touch the base table, we seem to always query for the
	 * ybctid, even if the table may have explicit primary keys.  A lower layer
	 * probably filters this out when not applicable.
	 */
	YbDmlAppendTargetSystem(YBTupleIdAttributeNumber, ybso->handle);
	/*
	 * For now, target all non-system columns of the base table.  This can be
	 * very inefficient.  The lsm index access method avoids this using
	 * filtering (see YbAddTargetColumnIfRequired).
	 *
	 * TODO(jason): don't target unnecessary columns.
	 */
	for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		if (!TupleDescAttr(tupdesc, attnum - 1)->attisdropped)
			YbDmlAppendTargetRegular(tupdesc, attnum, ybso->handle);
	}
}

/*
 * With select prepared, ask pggate to execute it for the first time.  This
 * will prefetch some rows.  Later fetches should not use this.
 */
static void
ybginExecSelect(IndexScanDesc scan, ScanDirection dir)
{
	YbginScanOpaque ybso = (YbginScanOpaque) scan->opaque;

	Assert(!ScanDirectionIsBackward(dir));
	/* Set scan direction, if matters */
	if (ScanDirectionIsForward(dir))
		HandleYBStatus(YBCPgSetForwardScan(ybso->handle, true));

	HandleYBStatus(YBCPgExecSelect(ybso->handle, NULL /* exec_params */));
}

/*
 * Prepare and request the initial execution of select to pggate.
 */
static bool
ybginDoFirstExec(IndexScanDesc scan, ScanDirection dir)
{
	YbginScanOpaque ybso = (YbginScanOpaque) scan->opaque;

	if (!ybginGetScanKeys(scan))
		return false;

	/* binds */
	ybginSetupBinds(scan);

	/* targets */
	if (scan->yb_aggrefs != NIL)
		/*
		 * As of 2023-06-28, aggregate pushdown is only implemented for
		 * IndexOnlyScan, not IndexScan.
		 */
		YbDmlAppendTargetsAggregate(scan->yb_aggrefs,
									RelationGetDescr(scan->indexRelation),
									scan->indexRelation,
									scan->xs_want_itup,
									ybso->handle);
	else
		ybginSetupTargets(scan);

	YbSetCatalogCacheVersion(ybso->handle, YbGetCatalogCacheVersion());

	/* execute select */
	ybginExecSelect(scan, dir);

	return true;
}

/*
 * Fetch the next tuple from pggate.
 */
static HeapTuple
ybginFetchNextHeapTuple(IndexScanDesc scan)
{
	bool		has_data = false;
	bool	   *nulls;
	Datum	   *values;
	HeapTuple	tuple = NULL;
	TupleDesc	tupdesc;
	YBCPgSysColumns syscols;
	YbginScanOpaque ybso = (YbginScanOpaque) scan->opaque;

	/*
	 * As an IndexScan, target the base table.  Change this if we ever support
	 * ybgin IndexOnlyScan.
	 */
	tupdesc = RelationGetDescr(scan->heapRelation);
	nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));

	HandleYBStatus(YBCPgDmlFetch(ybso->handle,
								 tupdesc->natts,
								 (uint64_t *) values,
								 nulls,
								 &syscols,
								 &has_data));
	if (has_data)
	{
		tuple = heap_form_tuple(tupdesc, values, nulls);

		tuple->t_tableOid = RelationGetRelid(scan->heapRelation);
		if (syscols.ybctid != NULL)
			HEAPTUPLE_YBCTID(tuple) = PointerGetDatum(syscols.ybctid);
	}
	pfree(values);
	pfree(nulls);

	return tuple;
}

bool
ybgingettuple(IndexScanDesc scan, ScanDirection dir)
{
	HeapTuple	tup;
	YbginScanOpaque ybso = (YbginScanOpaque) scan->opaque;

	/* Sanity check: amcanbackward. */
	Assert(!ScanDirectionIsBackward(dir));

	if (!ybso->is_exec_done)
	{
		if (!ybginDoFirstExec(scan, dir))
			return false;
		ybso->is_exec_done = true;
	}

	/* fetch */
	if (scan->yb_aggrefs)
	{
		/*
		 * TODO(jason): don't assume that recheck is needed.
		 */
		scan->xs_recheck = true;

		/*
		 * Aggregate pushdown directly modifies the scan slot rather than
		 * passing it through xs_hitup or xs_itup.
		 */
		return ybc_getnext_aggslot(scan, ybso->handle, scan->xs_want_itup);
	}
	while (HeapTupleIsValid(tup = ybginFetchNextHeapTuple(scan)))
	{
		if (true)				/* TODO(jason): don't assume a match. */
		{
			scan->xs_hitup = tup;
			scan->xs_hitupdesc = RelationGetDescr(scan->heapRelation);

			/* TODO(jason): don't assume that recheck is needed. */
			scan->xs_recheck = true;
			return true;
		}

		heap_freetuple(tup);
	}

	return false;
}

/*
 * TODO(jason): don't assume that recheck is needed.
 */
bool
ybginmightrecheck(Relation heapRelation, Relation indexRelation,
				  bool xs_want_itup, ScanKey keys, int nkeys)
{
	return true;
}
